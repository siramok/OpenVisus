/*-----------------------------------------------------------------------------
Copyright(c) 2010 - 2018 ViSUS L.L.C.,
Scientific Computing and Imaging Institute of the University of Utah

ViSUS L.L.C., 50 W.Broadway, Ste. 300, 84101 - 2044 Salt Lake City, UT
University of Utah, 72 S Central Campus Dr, Room 3750, 84112 Salt Lake City, UT

All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met :

* Redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.

* Neither the name of the copyright holder nor the names of its
contributors may be used to endorse or promote products derived from
this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED.IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

For additional information about this project contact : pascucci@acm.org
For support : support@visus.net
-----------------------------------------------------------------------------*/

#include <Visus/DatasetFilter.h>
#include <Visus/DatasetBitmask.h>
#include <Visus/Dataset.h>

namespace Visus {

///////////////////////////////////////////////////////////////////////////////////
DatasetFilter::DatasetFilter(Dataset* dataset_,const Field& field,int filtersize_,String name_) 
  : dataset(dataset_),dtype(field.dtype), size(filtersize_),name(name_),bNeedExtraComponent(false)
{
  VisusAssert(size>0);
}

///////////////////////////////////////////////////////////////////////////////////
DatasetFilter::~DatasetFilter()
{}



///////////////////////////////////////////////////////////////////////////////////
PointNi DatasetFilter::getFilterStep(int H) const
{
  /* Example ('-' means the same group for the filter): 
    
  V000 with filter_size=2
    (V) H=0 step=8  filterstep=16    00
    (0) H=1 step=4  filterstep=8     00----------04
    (0) H=2 step=2  filterstep=4     00----02    04----06
    (0) H=3 step=1  filterstep=2     00-01 02-03 04-05 06-07

  V0000 with filter_size=3
    (V) H=0 step=16  filterstep=48   00
    (0) H=1 step=8   filterstep=24   00                      08
    (0) H=2 step=4   filterstep=12   00----------04----------08          12
    (0) H=3 step=2   filterstep=6    00----02----04    06----08----10    12----14
    (0) H=4 step=1   filterstep=3    00-01-02 03-04-05 06-07-08 09-10-11 12-13-14 15

  For 2d/3d... is more complex. But keep in mind that the "restriction" that filterstep
  applies, are inherited going from fine to coarse resolutions. So for example (read bottom to top
  as you are trying to know samples dependencies):

  V0101 (dimensions 4*4) filter_size=2
        
    See query-filter-explanation.gif

    ^ (V)  step(4,4) filterstep(8,8) 
    | (0)  step(2,2) filterstep(4,4) 
    | (1)  step(1,2) filterstep(2,4) 
    | (0)  step(1,1) filterstep(2,2) 
    | (1)  step(0,1) filterstep(1,2) 
  */

  DatasetBitmask bitmask=dataset->getBitmask();
  int pdim = bitmask.getPointDim();
  PointNi step= bitmask.getPow2Box().size();
  for (int K=0;K<H;K++)
  {
    int bit=bitmask[K];
    if (!K) 
      step=step.rightShift(PointNi::one(pdim));
    else //
      step[bit]>>=1;
  }

  //note the std::max.. don't want 0!
  PointNi filterstep=PointNi::one(pdim);
  for (int D=0;D<pdim;D++) 
    filterstep[D]=std::max((Int64)1,step[D]*this->size);

  return filterstep;
}


///////////////////////////////////////////////////////////////////////////////
bool DatasetFilter::computeFilter(double time,Field field,SharedPtr<Access> access,PointNi SlidingWindow) const
{
  //this works only for filter_size==2, otherwise the building of the sliding_window is very difficult
  VisusAssert(this->size==2);

  DatasetBitmask bitmask   = dataset->getBitmask();
  BoxNi          box       = dataset->getLogicBox();

  int pdim = bitmask.getPointDim();

  //the window size must be multiple of 2, otherwise I loose the filter alignment
  for (int D=0;D<pdim;D++)
  {
    Int64 size=SlidingWindow[D];
    VisusAssert(size==1 || (size/2)*2==size);
  }

  //convert the dataset  FINE TO COARSE, this can be really time consuming!!!!
  for (int H=dataset->getMaxResolution();H>=1;H--)
  {
    PrintInfo("Applying filter to dataset resolution",H);
    int bit=bitmask[H];
      
    Int64 FILTERSTEP=this->getFilterStep(H)[bit];

    //need to align the from so that the first sample is filter-aligned
    PointNi From = box.p1;

    if (!Utils::isAligned(From[bit],(Int64)0,FILTERSTEP))
      From[bit]=Utils::alignLeft(From[bit],(Int64)0,FILTERSTEP)+FILTERSTEP; 

    PointNi To = box.p2;
    for (auto P = ForEachPoint(From, To, SlidingWindow); !P.end(); P.next())
    {
      //this is the sliding window
      BoxNi sliding_window(P.pos,P.pos +SlidingWindow);

      //important! crop to the stored world box to be sure that the alignment with the filter is correct!
      sliding_window= sliding_window.getIntersection(box);

      //no valid box since do not intersect with box 
      if (!sliding_window.isFullDim())
        continue;

      //I'm sure that since the From is filter-aligned, then P must be already aligned
      VisusAssert(Utils::isAligned(sliding_window.p1[bit],(Int64)0,FILTERSTEP));

      //important, i'm not using adjustBox because I'm sure it is already correct!
      auto read=std::make_shared<BoxQuery>(dataset, field, time,'r');
      read->logic_box=sliding_window;
      read->setResolutionRange(0,H);

      dataset->beginQuery(read);

      if (!dataset->executeQuery(access,read))
        return false;

      //if you want to debug step by step...
      #if 0
      {
        PrintInfo("Before");
        int nx=(int)read->buffer->dims.x;
        int ny=(int)read->buffer->dims.y;
        Uint8* SRC=read->buffer->c_ptr();
        std::ostringstream out;
        for (int Y=0;Y<ny;Y++)
        {
          for (int X=0;X<nx;X++)
          {
            out <<std::setw(3)<<(int)SRC[((ny-Y-1)*nx+X)*2]<<" ";
          }
          out <<std::endl;
        }
        out <<std::endl;
        PrintInfo("\n" << out.str();
      }
      #endif

      if (!computeFilter(read.get(),/*bInverse*/false))
        return false;

      //if you want to debug step by step...
      #if 0 
      {
        PrintInfo("After");
        int nx=(int)read->buffer->dims.x;
        int ny=(int)read->buffer->dims.y;
        Uint8* SRC=read->buffer->c_ptr();
        std::ostringstream out;
        for (int Y=0;Y<ny;Y++)
        {
          for (int X=0;X<nx;X++)
          {
            out <<std::setw(3)<<(int)SRC[((ny-Y-1)*nx+X)*2]<<" ";
          }
          out <<std::endl;
        }
        out <<std::endl;
        PrintInfo("\n",out.str());
      }
      #endif

      auto write=std::make_shared<BoxQuery>(dataset, field, time,'w');
      write->logic_box=sliding_window;
      write->setResolutionRange(0,H);

      dataset->beginQuery(write);

      if (!write->isRunning())
        return false;

      write->buffer=read->buffer;

      if (!dataset->executeQuery(access,write))
        return false;
    }

    //I'm going to write the next resolution, double the dimension along the current axis
    //in this way I have the same number of samples!
    SlidingWindow[bit]<<=1;
  }
  return true;
}



} //namespace Visus