/***************************************************
** ViSUS Visualization Project                    **
** Copyright (c) 2010 University of Utah          **
** Scientific Computing and Imaging Institute     **
** 72 S Central Campus Drive, Room 3750           **
** Salt Lake City, UT 84112                       **
**                                                **
** For information about this project see:        **
** http://www.pascucci.org/visus/                 **
**                                                **
**      or contact: pascucci@sci.utah.edu         **
**                                                **
****************************************************/

#if WIN32
#pragma warning(disable:4251 4267 4996 4244 4251)
#endif

#include "slam.h"

#include <g2o/core/block_solver.h>
#include <g2o/core/optimization_algorithm_levenberg.h>
#include <g2o/core/robust_kernel_impl.h>
#include <g2o/types/sba/types_six_dof_expmap.h>
#include <g2o/core/base_multi_edge.h>
#include <g2o/solvers/eigen/linear_solver_eigen.h>
#include <g2o/core/sparse_optimizer_terminate_action.h>

#include <map>
#include <fstream>
#include <iterator>
#include <thread>
#include <numeric>

#if WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

namespace Visus {

//////////////////////////////////////////////////////////////////////////////////
class BundleAdjustment
{
public:

  typedef Eigen::Matrix<double, 2, 1> Vector2d;
  typedef Eigen::Matrix<double, 3, 1> Vector3d;
  typedef Eigen::Matrix<double, 4, 1> Vector4d;
  typedef Eigen::Matrix<double, 5, 1> Vector5d;
  typedef Eigen::Matrix<double, 6, 1> Vector6d;

  //___________________________________________________________________
  //see https://github.com/RainerKuemmerle/g2o/blob/master/g2o/core/sparse_optimizer_terminate_action.cpp
  class MyPostIterationAction : public g2o::HyperGraphAction
  {
  public:

    BundleAdjustment* ba;
    bool              terminate_flag = false;
    double            lastChi = 0;

    //constructor
    MyPostIterationAction(BundleAdjustment* ba_) : ba(ba_) {
    }

    //operator()
    virtual HyperGraphAction* operator()(const g2o::HyperGraph* graph, Parameters* parameters = 0) override
    {
      g2o::SparseOptimizer* optimizer = static_cast<g2o::SparseOptimizer*>(const_cast<g2o::HyperGraph*>(graph));
      HyperGraphAction::ParametersIteration* params = static_cast<HyperGraphAction::ParametersIteration*>(parameters);

      bool stop = false;
      auto iteration = params->iteration;
      if (iteration >= 0)
      {
        optimizer->computeActiveErrors();
        double currentChi = optimizer->activeRobustChi2();
        PrintInfo("Bundle adjustment activeRobustChi2(", currentChi, ")");
        if (iteration > 0)
        {
          double gain = (lastChi - currentChi) / currentChi;
          if (gain >= 0 && gain < ba->gainThreshold)
            stop = true;
        }
        lastChi = currentChi;

        //callbacks
        ba->updateSolution();
        ba->slam->doPostIterationAction();
      }

      if (optimizer->forceStopFlag())
      {
        *(optimizer->forceStopFlag()) = stop;
      }
      else
      {
        terminate_flag = stop;
        optimizer->setForceStopFlag(&terminate_flag);
      }

      return this;
    }
  };

  Slam* slam;
  g2o::SparseOptimizer* optimizer = nullptr;
  int                   VertexId = 0;
  double                gainThreshold;

  //constructor
  BundleAdjustment(Slam* slam_, double gainThreshold_) : slam(slam_), gainThreshold(gainThreshold_)
  {
    PrintInfo("Starting bundle adjustment...");

    //create optimizer
    optimizer = new g2o::SparseOptimizer();

    typedef g2o::BlockSolver< g2o::BlockSolverTraits<Eigen::Dynamic, Eigen::Dynamic> > SlamBlockSolver;
    typedef g2o::LinearSolverEigen<SlamBlockSolver::PoseMatrixType>                    SlamLinearSolver;

    VertexId = 0;

    auto linear_solver = new SlamLinearSolver();
    auto block_solver = new SlamBlockSolver(linear_solver);
    optimizer->setAlgorithm(new g2o::OptimizationAlgorithmLevenberg(block_solver));
  }

  //destructor
  virtual ~BundleAdjustment() {
    delete optimizer;
  }

  //setInitialSolution
  virtual void setInitialSolution() = 0;

  //updateSolution
  virtual void updateSolution() = 0;

  //doBundleAdjustment
  virtual void doBundleAdjustment()
  {
    Time t1 = Time::now();

    setInitialSolution();

    optimizer->addPostIterationAction(new MyPostIterationAction(this));
    optimizer->initializeOptimization();
    optimizer->optimize(std::numeric_limits<int>::max());

    PrintInfo(" bundleAdjustment done in ", t1.elapsedMsec(), "msec", " #cameras(", slam->cameras.size(), ")");
  }

};

  //////////////////////////////////////////////////////////////////////////////////
  class SlamBundleAdjustment : public BundleAdjustment
  {
  public:

    //___________________________________________________________________
    class BACameraVertex : public g2o::BaseVertex<6, g2o::SE3Quat> {
    public:

      EIGEN_MAKE_ALIGNED_OPERATOR_NEW

        //constructor
        BACameraVertex() {
      }

      //constructor
      BACameraVertex(Camera* camera)
      {
        auto q = camera->pose.q;
        auto t = camera->pose.t;
        setEstimate(g2o::SE3Quat(Eigen::Quaterniond(q.w, q.x, q.y, q.z), Vector3d(t.x, t.y, t.z)));
      }

      virtual bool read(std::istream& is)        override { VisusReleaseAssert(false); return true; }
      virtual bool write(std::ostream& os) const override { VisusReleaseAssert(false); return true; }

      virtual void setToOriginImpl() override {
        _estimate = g2o::SE3Quat();
      }

      //oplusImpl
      virtual void oplusImpl(const double* update_) override {
        Eigen::Map<const Vector6d> update(update_);
        setEstimate(g2o::SE3Quat::exp(update) * estimate());
      }

    };

    //___________________________________________________________________
    class BACalibrationVertex : public g2o::BaseVertex<3, Vector3d>
    {
    public:

      EIGEN_MAKE_ALIGNED_OPERATOR_NEW

        //constructor
        BACalibrationVertex(const Calibration& calibration) {
        setEstimate(Vector3d(calibration.f, calibration.cx, calibration.cy));
      }

      virtual bool read(std::istream& is)        override { VisusReleaseAssert(false); return true; }
      virtual bool write(std::ostream& os) const override { VisusReleaseAssert(false); return true; }

      //setToOriginImpl
      virtual void setToOriginImpl() override
      {
        _estimate << 1.0, 0.0, 0.0;
      }

      //oplusImpl
      virtual void oplusImpl(const double* update_) override
      {
        Vector3d update(update_);
        setEstimate(estimate() + update);
      }
    };

    //___________________________________________________________________
    class BAPose
    {
    public:

      const g2o::SE3Quat& w2c;
      g2o::SE3Quat        c2w;
      const Calibration&  calibration;

      //constructor
      BAPose(const BACameraVertex* vertex, const Calibration& calibration_)
        : w2c(vertex->estimate()), calibration(calibration_) {
        c2w = w2c.inverse();
      }

      //cameraToWorld
      Point3d cameraToWorld(const Point3d& eye) const {
        auto ret = c2w.map(Vector3d(eye.x, eye.y, eye.z));
        return Point3d(ret.x(), ret.y(), ret.z());
      }

      //worldToCamera
      Point3d worldToCamera(const Point3d& world) const {
        auto ret = w2c.map(Vector3d(world.x, world.y, world.z));
        return Point3d(ret.x(), ret.y(), ret.z());
      }

      //worldToScreen
      Point2d worldToScreen(const Point3d& world) const {
        return calibration.cameraToScreen(worldToCamera(world));
      }

      //getWorldRay
      Ray getWorldRay(const Point2d& screen) const {
        auto w0 = cameraToWorld(Point3d());
        auto w1 = cameraToWorld(calibration.screenToCamera(screen));
        return Ray::fromTwoPoints(w0, w1);
      }

    };

    //___________________________________________________________________
    class BAEdge : public g2o::BaseMultiEdge<
      /*error_dim*/4,
      /*error_vector*/Vector4d>
    {
    public:

      EIGEN_MAKE_ALIGNED_OPERATOR_NEW

        Point2d s1;
      Point2d s2;

      //constructor
      BAEdge(Camera* camera1, const Point2d& s1_, Camera* camera2, const Point2d& s2_, BACalibrationVertex* Kvertex)
        : s1(s1_), s2(s2_)
      {
        resize(3); //num vertices

        setVertex(0, static_cast<BACameraVertex*>(camera1->ba.vertex));
        setVertex(1, static_cast<BACameraVertex*>(camera2->ba.vertex));
        setVertex(2, Kvertex);

        setMeasurement(Vector4d(s1.x, s1.y, s2.x, s2.y));
        setInformation(Eigen::Matrix4d::Identity());
        setRobustKernel(new g2o::RobustKernelHuber());
      }

      virtual bool read(std::istream& is)        override { VisusReleaseAssert(false); return true; }
      virtual bool write(std::ostream& os) const override { VisusReleaseAssert(false); return true; }

      //virtual void linearizeOplus() override; NOTE this can be faster If I knew how to compute the Jacobian

      //computeError
      virtual void computeError() override
      {
        auto v0 = (BACameraVertex*)_vertices[0];
        auto v1 = (BACameraVertex*)_vertices[1];
        auto v2 = (BACalibrationVertex*)_vertices[2];

        const Vector3d& K = v2->estimate();

        Calibration calibration(K.x(), K.y(), K.z());

        BAPose camera1(v0, calibration);
        BAPose camera2(v1, calibration);

        auto S1 = camera1.worldToScreen(camera2.getWorldRay(s2).findIntersectionOnZeroPlane().toPoint3());
        auto S2 = camera2.worldToScreen(camera1.getWorldRay(s1).findIntersectionOnZeroPlane().toPoint3());

        _error[0] = S1.x - s1.x;
        _error[1] = S1.y - s1.y;
        _error[2] = S2.x - s2.x;
        _error[3] = S2.y - s2.y;
      }
    };

    BACalibrationVertex* Kvertex = nullptr;

    //constructor
    SlamBundleAdjustment(Slam* slam_, double gainThreshold_)
      : BundleAdjustment(slam_, gainThreshold_)
    {
    }

    //destructor
    virtual ~SlamBundleAdjustment() {
    }

    //setInitialSolution
    virtual void setInitialSolution() override
    {
      //add calibration vertex
      Kvertex = new BACalibrationVertex(slam->calibration);
      Kvertex->setId(VertexId++);
      Kvertex->setFixed(slam->calibration.bFixed);
      optimizer->addVertex(Kvertex);

      //add camera vertex
      for (auto camera : slam->cameras)
      {
        auto vertex = new BACameraVertex(camera);
        vertex->setId(VertexId++);
        vertex->setFixed(camera->bFixed);
        optimizer->addVertex(vertex);
        camera->ba.vertex = vertex;
      }


      //add matches (edge)
      for (auto camera2 : slam->cameras)
      {
        for (auto camera1 : camera2->getGoodLocalCameras())
        {
          if (camera1->id < camera2->id)
          {
            for (auto match : camera2->getEdge(camera1)->matches)
            {
              auto s1 = Point2d(camera1->keypoints[match.queryIdx].x, camera1->keypoints[match.queryIdx].y);
              auto s2 = Point2d(camera2->keypoints[match.trainIdx].x, camera2->keypoints[match.trainIdx].y);
              optimizer->addEdge(new BAEdge(camera1, s1, camera2, s2, Kvertex));
            }
          }
        }
      }
    }

    //updateSolution
    virtual void updateSolution() override
    {
      Vector3d est = Kvertex->estimate();
      slam->calibration.f = est[0];
      slam->calibration.cx = est[1];
      slam->calibration.cy = est[2];

      for (auto camera : slam->cameras)
      {
        auto vertex = (BACameraVertex*)camera->ba.vertex;
        auto w2c = vertex->estimate();
        g2o::SE3Quat e(w2c);
        auto R = Quaternion(e.rotation().w(), e.rotation().x(), e.rotation().y(), e.rotation().z());
        auto t = Point3d(e.translation().x(), e.translation().y(), e.translation().z());
        camera->pose = Pose(R, t);
      }

      slam->refreshQuads();

      auto new_calibration = slam->calibration;
      PrintInfo(" K1", new_calibration.f, " ", new_calibration.cx, " ", new_calibration.cy, ")");
    }


  };
    //////////////////////////////////////////////////////////////////////////////////
    class LocalBundleAdjustment : public SlamBundleAdjustment {
    public:
        const std::vector<int>& mask;
        LocalBundleAdjustment(Slam* slam_, double gainThreshold_, const std::vector<int>& mask_) : SlamBundleAdjustment(slam_, gainThreshold_), mask(mask_) {
        }

        //doBundleAdjustment
        void doBundleAdjustment() override
        {
            Time t1 = Time::now();
            std::vector<Camera*> temp;
            for (int i = 0; i < slam->cameras.size(); ++i)
            {
                if (mask.at(i))
                {
                    temp.push_back(slam->cameras.at(i));
                }
            }
            slam->cameras.swap(temp);

            setInitialSolution();

            optimizer->addPostIterationAction(new MyPostIterationAction(this));
            optimizer->initializeOptimization();
            optimizer->optimize(std::numeric_limits<int>::max());

            PrintInfo(" bundleAdjustment done in ", t1.elapsedMsec(), "msec", " #cameras(", slam->cameras.size(), ")");
            slam->cameras.swap(temp);
        }

        //setInitialSolution
        void setInitialSolution() override
        {
            //add calibration vertex
            Kvertex = new BACalibrationVertex(slam->calibration);
            Kvertex->setId(VertexId++);
            Kvertex->setFixed(slam->calibration.bFixed);
            optimizer->addVertex(Kvertex);

//            //add camera vertex
//            for (auto camera : slam->cameras)
//            {
//                auto vertex = new BACameraVertex(camera);
//                vertex->setId(VertexId++);
//                vertex->setFixed(camera->bFixed);
//                optimizer->addVertex(vertex);
//                camera->ba.vertex = vertex;
//            }

            //add matches (edge)
            for (auto camera2 : slam->cameras)
            {
                auto vertex = new BACameraVertex(camera2);
                vertex->setId(VertexId++);
                vertex->setFixed(camera2->bFixed);
                optimizer->addVertex(vertex);
                camera2->ba.vertex = vertex;
                for (auto camera1 : slam->cameras)
                {
                    if (camera1->id < camera2->id)
                    {
                        for (auto match : camera2->getEdge(camera1)->matches)
                        {
                            auto s1 = Point2d(camera1->keypoints[match.queryIdx].x, camera1->keypoints[match.queryIdx].y);
                            auto s2 = Point2d(camera2->keypoints[match.trainIdx].x, camera2->keypoints[match.trainIdx].y);
                            optimizer->addEdge(new BAEdge(camera1, s1, camera2, s2, Kvertex));
                        }
                    }
                }
            }
        }
    };

  //////////////////////////////////////////////////////////////////////////////////
  class OffsetBundleAdjustment : public BundleAdjustment
  {
  public:

    //___________________________________________________________________
    class BAVertex : public g2o::BaseVertex<2, Vector2d> {
    public:

      EIGEN_MAKE_ALIGNED_OPERATOR_NEW

        //constructor
        BAVertex() {
      }

      //constructor
      BAVertex(Camera* camera)
      {
        setEstimate(Vector2d(camera->homography(0, 2), camera->homography(1, 2)));
      }

      //not supported
      virtual bool read(std::istream& is)        override { VisusReleaseAssert(false); return true; }
      virtual bool write(std::ostream& os) const override { VisusReleaseAssert(false); return true; }

      //setToOriginImpl
      virtual void setToOriginImpl() override {
        _estimate = Vector2d(0, 0);
      }

      //oplusImpl
      virtual void oplusImpl(const double* update_) override {
        Vector2d update(update_);
        setEstimate(estimate() + update);
      }

    };

    //___________________________________________________________________
    class BAEdge : public g2o::BaseMultiEdge</*error_dim*/4,/*error_vector*/Vector4d>
    {
    public:

      EIGEN_MAKE_ALIGNED_OPERATOR_NEW

        Point2d s1;
      Point2d s2;

      //constructor
      BAEdge(Camera* camera1, const Point2d& s1_, Camera* camera2, const Point2d& s2_)
        : s1(s1_), s2(s2_)
      {
        resize(2); //num vertices

        setVertex(0, static_cast<BAVertex*>(camera1->ba.vertex));
        setVertex(1, static_cast<BAVertex*>(camera2->ba.vertex));

        setMeasurement(Vector4d(s1.x, s1.y, s2.x, s2.y));
        setInformation(Eigen::Matrix4d::Identity());
        setRobustKernel(new g2o::RobustKernelHuber());
      }

      //not supported
      virtual bool read(std::istream& is)        override { VisusReleaseAssert(false); return true; }
      virtual bool write(std::ostream& os) const override { VisusReleaseAssert(false); return true; }

      //virtual void linearizeOplus() override; NOTE this can be faster If I knew how to compute the Jacobian

      //computeError
      virtual void computeError() override
      {
        Vector2d camera1 = ((BAVertex*)_vertices[0])->estimate();
        Vector2d camera2 = ((BAVertex*)_vertices[1])->estimate();

        auto S1 = s2 + Point2d(camera2[0], camera2[1]) - Point2d(camera1[0], camera1[1]);
        auto S2 = s1 + Point2d(camera1[0], camera1[1]) - Point2d(camera2[0], camera2[1]);

        _error[0] = S1.x - s1.x;
        _error[1] = S1.y - s1.y;
        _error[2] = S2.x - s2.x;
        _error[3] = S2.y - s2.y;
      }
    };

    //constructor
    OffsetBundleAdjustment(Slam* slam_, double gainThreshold_) : BundleAdjustment(slam_, gainThreshold_) {
    }

    //destructor
    virtual ~OffsetBundleAdjustment() {
    }

    //setInitialSolution
    virtual void setInitialSolution() override
    {
      for (auto camera : slam->cameras)
      {
        auto vertex = new BAVertex(camera);
        vertex->setId(VertexId++);
        vertex->setFixed(false);
        optimizer->addVertex(vertex);
        camera->ba.vertex = vertex;
      }

      //add matches (edge)
      for (auto camera2 : slam->cameras)
      {
        for (auto camera1 : camera2->getGoodLocalCameras())
        {
          if (camera1->id < camera2->id)
          {
            for (auto match : camera2->getEdge(camera1)->matches)
            {
              auto s1 = Point2d(camera1->keypoints[match.queryIdx].x, camera1->keypoints[match.queryIdx].y);
              auto s2 = Point2d(camera2->keypoints[match.trainIdx].x, camera2->keypoints[match.trainIdx].y);
              optimizer->addEdge(new BAEdge(camera1, s1, camera2, s2));
            }
          }
        }
      }
    }

    //updateSolution 
    virtual void updateSolution() override
    {
      for (auto camera : slam->cameras)
      {
        auto offset = ((BAVertex*)camera->ba.vertex)->estimate();
        camera->homography = Matrix::translate(Point2d(offset[0], offset[1]));
        camera->quad = Quad(camera->homography, Quad(slam->width, slam->height));
      }
    }

  };


  ///////////////////////////////////////////////////////////////////////////////
  void Slam::bundleAdjustment(double ba_tolerance, String algorithm)
  {
    if (algorithm == "offset")
      OffsetBundleAdjustment(this, ba_tolerance).doBundleAdjustment();
    else
      SlamBundleAdjustment(this, ba_tolerance).doBundleAdjustment();
  }

  void Slam::localBundleAdjustment(double ba_tolerance, const std::vector<int>& mask) {
      LocalBundleAdjustment(this, ba_tolerance, mask).doBundleAdjustment();
  }

}//end namespace




