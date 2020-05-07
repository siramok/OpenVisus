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

#ifndef VISUS_JTREE_RENDER_NODE_H__
#define VISUS_JTREE_RENDER_NODE_H__

#include <Visus/Gui.h>
#include <Visus/DataflowNode.h>
#include <Visus/Graph.h>
#include <Visus/GLMesh.h>
#include <Visus/GLCanvas.h>
#include <Visus/GuiFactory.h>

#include <QFrame>

namespace Visus {

////////////////////////////////////////////////////////////////////
// Receive a FGraph data and display it as it is
class VISUS_GUI_API JTreeRenderNode : 
  public Node, 
  public GLObject
{
public:

  VISUS_NON_COPYABLE_CLASS(JTreeRenderNode)

  // construct a join tree or a split tree
  JTreeRenderNode(); 

  //destructor
  virtual ~JTreeRenderNode();

  //getRadius
  double getRadius() const {
    return radius;
  }

  //setRadius
  void setRadius(double value) {
    setProperty("SetRadius", this->radius, value);
  }

  //getMinMaterial
  GLMaterial getMinMaterial() const {
    return min_material;
  }

  //setMinMaterial
  void setMinMaterial(GLMaterial value) {
    setEncodedProperty("SetMinMaterial", this->min_material, value);
  }

  //getMaxMaterial
  GLMaterial getMaxMaterial() const {
    return max_material;
  }

  //setMaxMaterial
  void setMaxMaterial(GLMaterial value) {
    setEncodedProperty("SetMaxMaterial", this->max_material, value);
  }

  //getSaddleMaterial
  GLMaterial getSaddleMaterial() const {
    return saddle_material;
  }

  //setSaddleMaterial
  void setSaddleMaterial(GLMaterial value) {
    setEncodedProperty("SetSaddleMaterial", this->saddle_material, value);
  }

  //drawEdges
  bool drawEdges() const {
    return draw_edges;
  }

  //setDrawEdges
  void setDrawEdges(bool value) {
    setProperty("SetDrawEdges", this->draw_edges, value);
  }

  //drawExtrema
  bool drawExtrema() const {
    return draw_extrema;
  }

  //setDrawExtrema
  void setDrawExtrema(bool value) {
    setProperty("SetDrawExtrema", this->draw_extrema, value);
  }

  //drawSaddles
  bool drawSaddles() const{
    return draw_saddles;
  }

  //setDrawSaddles
  void setDrawSaddles(bool value) {
    setProperty("SetDrawSaddles", this->draw_saddles, value);
  }

  //is2d
  bool is2d() const {
    return is_2d;
  }

  //set2d
  void set2d(bool value) {
    setProperty("Set2d", this->is_2d, value);
  }

  //colorByComponent
  bool colorByComponent() const{
    return color_by_component;
  }

  //setColorByComponent
  void setColorByComponent(bool value) {
    setProperty("SetColorByComponent", this->color_by_component, value);
  }

  //glRender
  virtual void glRender(GLCanvas& gl) override;

  //getBounds
  virtual Position getBounds() override;

  //from Node
  virtual bool processInput() override;

  //glGetRenderQueue
  virtual int glGetRenderQueue() const override {
    return is_2d ? 9900 : -1;
  }

public:

  //execute
  virtual void execute(Archive& ar) override;

  //write
  virtual void write(Archive& ar) const override;

  //read
  virtual void read(Archive& ar) override;

private:

  SharedPtr<FGraph> graph; 

  double         radius = 2.5;
  GLMaterial     min_material;
  GLMaterial     max_material;
  GLMaterial     saddle_material;
  bool           draw_edges = false;
  bool           draw_extrema = false;
  bool           draw_saddles = false;
  bool           is_2d = false;
  bool           color_by_component = true;

}; //end class


/////////////////////////////////////////////////////////
class VISUS_GUI_API JTreeRenderNodeView :
  public QFrame,
  public View<JTreeRenderNode>
{
public:

  VISUS_NON_COPYABLE_CLASS(JTreeRenderNodeView)

    //constructor
    JTreeRenderNodeView(JTreeRenderNode* model = nullptr) {
    if (model)
      bindModel(model);
  }

  //destructor
  virtual ~JTreeRenderNodeView() {
    bindModel(nullptr);
  }

  //bindModel
  virtual void bindModel(JTreeRenderNode* value) override
  {
    if (this->model)
    {
      QUtils::clearQWidget(this);
      widgets = Widgets();
    }

    View<ModelClass>::bindModel(value);

    if (this->model)
    {
      QFormLayout* layout = new QFormLayout();

      layout->addRow("color_by_component", widgets.color_by_component = GuiFactory::CreateCheckBox(model->colorByComponent(), "", [this](int value) {
        model->setColorByComponent((bool)value);
      }));

      layout->addRow("draw_saddles", widgets.draw_saddles = GuiFactory::CreateCheckBox(model->drawSaddles(), "", [this](int value) {
        model->setDrawSaddles((bool)value);
      }));

      layout->addRow("draw_extrema", widgets.draw_extrema = GuiFactory::CreateCheckBox(model->drawExtrema(), "", [this](int value) {
        model->setDrawExtrema((bool)value);
      }));

      layout->addRow("draw_edges", widgets.draw_edges = GuiFactory::CreateCheckBox(model->drawEdges(), "", [this](int value) {
        model->setDrawEdges((bool)value);
      }));

      layout->addRow("is_2d", widgets.is_2d = GuiFactory::CreateCheckBox(model->is2d(), "", [this](int value) {
        model->set2d((bool)value);
      }));

      layout->addRow("radius", widgets.radius = GuiFactory::CreateDoubleSliderWidget(model->getRadius(), Range(0.1, 10, 0), [this](double value) {
        model->setRadius(value);
      }));

      layout->addRow("min_material", widgets.min_material = GuiFactory::CreateGLMaterialView(model->getMinMaterial(), [this](GLMaterial value) {
        model->setMinMaterial(value);
      }));

      layout->addRow("max_material", widgets.max_material = GuiFactory::CreateGLMaterialView(model->getMaxMaterial(), [this](GLMaterial value) {
        model->setMaxMaterial(value);
      }));

      layout->addRow("saddle_material", widgets.saddle_material = GuiFactory::CreateGLMaterialView(model->getSaddleMaterial(), [this](GLMaterial value) {
        model->setSaddleMaterial(value);
      }));

      setLayout(layout);
      refreshGui();
    }
  }

private:

  class Widgets
  {
  public:
    QCheckBox* color_by_component = nullptr;
    QCheckBox* draw_saddles = nullptr;
    QCheckBox* draw_extrema = nullptr;
    QCheckBox* draw_edges = nullptr;
    QCheckBox* is_2d = nullptr;
    QDoubleSlider* radius = nullptr;
    GuiFactory::GLMaterialView* min_material = nullptr;
    GuiFactory::GLMaterialView* max_material = nullptr;
    GuiFactory::GLMaterialView* saddle_material = nullptr;
  };

  Widgets widgets;

  ///refreshGui
  void refreshGui()
  {
    widgets.color_by_component->setChecked(model->colorByComponent());
    widgets.draw_saddles->setChecked(model->drawSaddles());
    widgets.draw_extrema->setChecked(model->drawExtrema());
    widgets.draw_edges->setChecked(model->drawEdges());
    widgets.is_2d->setChecked(model->is2d());
    widgets.radius->setValue(model->getRadius());
    widgets.min_material->setMaterial(model->getMinMaterial());
    widgets.max_material->setMaterial(model->getMaxMaterial());
    widgets.saddle_material->setMaterial(model->getSaddleMaterial());
  }

  //modelChanged
  virtual void modelChanged() override {
    refreshGui();
  }

};

} //namespace Visus

#endif // VISUS_JTREE_RENDER_NODE_H__