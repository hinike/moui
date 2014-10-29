// Copyright (c) 2014 Ollix. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// ---
// Author: olliwang@ollix.com (Olli Wang)

#include "moui/widgets/widget_view.h"

#include <cassert>
#include <stack>
#include <vector>

#include "moui/core/device.h"
#include "moui/core/event.h"
#include "moui/nanovg_hook.h"
#include "moui/opengl_hook.h"
#include "moui/ui/native_view.h"
#include "moui/ui/view.h"
#include "moui/widgets/widget.h"

namespace moui {

WidgetView::WidgetView() : View(), context_(nullptr), event_responder_(nullptr),
                           is_opaque_(true), root_widget_(new Widget) {
  root_widget_->set_widget_view(this);
}

WidgetView::~WidgetView() {
  if (context_ != nullptr) {
    nvgDeleteGL(context_);
  }
  delete root_widget_;
}

void WidgetView::AddWidget(Widget* widget) {
  root_widget_->AddChild(widget);
}

void WidgetView::HandleEvent(std::unique_ptr<Event> event) {
  if (event_responder_ == nullptr)
    return;
  // Resets the event responder if the widget view of the current responder
  // widget is not this widget view object. This may happen when the responder
  // widget is deattached from this widget view.
  if (event_responder_->widget_view() != this) {
    event_responder_ = nullptr;
    return;
  }
  // Asks the current responder widget to handle the event.
  event_responder_->HandleEvent(event.get());
}

void WidgetView::PopAndFinalizeWidgetItems(const int level,
                                           WidgetItemStack* stack) {
  while (!stack->empty()) {
    WidgetItem* top_item = stack->top();
    if (top_item->level < level)
      break;

    stack->pop();
    top_item->widget->WidgetDidRender(context_);
    delete top_item;
    nvgRestore(context_);
  }
}

// If the passed widget is visible on screen. Creates a WidgetItem object for
// the widget and adds it to the `widget_list`. Then repeats this process for
// its child widgets.
void WidgetView::PopulateWidgetList(const int level, const float scale,
                                    WidgetList* widget_list, Widget* widget,
                                    WidgetItem* parent_item) {
  if (widget->IsHidden()) return;
  const float kWidgetWidth = widget->GetWidth();
  if (kWidgetWidth <= 0) return;
  const float kWidgetHeight = widget->GetHeight();
  if (kWidgetHeight <= 0) return;
  const float kWidgetX = widget->GetX();
  const float kWidgetY = widget->GetY();
  const float kScaledWidgetWidth = kWidgetWidth * scale * widget->scale();
  const float kScaledWidgetHeight = kWidgetHeight * scale * widget->scale();

  // Determines the translate origin and the scissor area.
  Point translated_origin = {0.0f, 0.0f};
  Point scissor_origin = {0.0f, 0.0f};
  float scissor_width = kScaledWidgetWidth;
  float scissor_height = kScaledWidgetHeight;
  if (parent_item != nullptr) {
    translated_origin.x = parent_item->translated_origin.x + kWidgetX * scale;
    translated_origin.y = parent_item->translated_origin.y + kWidgetY * scale;
    // Determines the scissor's horizontal position.
    scissor_origin.x = std::max(parent_item->scissor_origin.x,
                                translated_origin.x);
    if (scissor_origin.x >= GetWidth()) return;
    // Determines the scissor's vertical position.
    scissor_origin.y = std::max(parent_item->scissor_origin.y,
                                translated_origin.y);
    if (scissor_origin.y >= GetHeight()) return;
    // Stops if the widget is invisible on the scissor's left or top.
    if ((translated_origin.x + kScaledWidgetWidth - 1) < scissor_origin.x ||
        (translated_origin.y + kScaledWidgetHeight - 1) < scissor_origin.y)
      return;
    // Determines the scissor width.
    const float kParentOriginX = parent_item->scissor_origin.x;
    scissor_width = std::min(
        scissor_width,
        kParentOriginX + parent_item->scissor_width - scissor_origin.x);
    scissor_width = std::min(
        scissor_width,
        scissor_origin.x + kScaledWidgetWidth - kParentOriginX);
    if (scissor_width <= 0 || (scissor_origin.x + scissor_width - 1) < 0)
      return;
    // Determines the scissor height.
    const float kParentOriginY = parent_item->scissor_origin.y;
    scissor_height = std::min(
        scissor_height,
        kParentOriginY + parent_item->scissor_height - scissor_origin.y);
    scissor_height = std::min(
        scissor_height,
        scissor_origin.y + kScaledWidgetHeight - kParentOriginY);
    if (scissor_height <= 0 || (scissor_origin.y + scissor_height - 1) < 0)
      return;
  }

  // The widget is visible. Adds it to the widget list and checks its children.
  auto item = new WidgetItem{widget, {kWidgetX, kWidgetY}, kWidgetWidth,
                             kWidgetHeight, level, parent_item,
                             widget->scale(), translated_origin,
                             scissor_origin, scissor_width, scissor_height};
  widget_list->push_back(item);
  const float kChildWidgetScale = scale * widget->scale();
  for (Widget* child : widget->children())
    PopulateWidgetList(level + 1, kChildWidgetScale, widget_list, child, item);
}

void WidgetView::Render() {
  if (context_ == nullptr) {
    context_ = nvgCreateGL(NVG_ANTIALIAS);
    root_widget_->UpdateContext(context_);
    ContextDidCreate(context_);
  }

  // Determines widgets to render in order and filters invisible onces.
  WidgetViewWillRender(root_widget_);
  WidgetList widget_list;
  PopulateWidgetList(0, 1, &widget_list, root_widget_, nullptr);

  // Renders offscreen stuff here so it won't interfere the onscreen rendering.
  for (WidgetItem* item : widget_list)
    item->widget->RenderDefaultFramebuffer(context_);

  // Renders visible widgets on screen.
  const float kWidth = GetWidth();
  const float kHeight = GetHeight();
  const float kScreenScaleFactor = Device::GetScreenScaleFactor();
  glViewport(0, 0, kWidth * kScreenScaleFactor, kHeight * kScreenScaleFactor);
  glClearColor(0, 0, 0, 0);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  nvgBeginFrame(context_, kWidth, kHeight, kScreenScaleFactor);
  WidgetItemStack rendering_stack;
  for (WidgetItem* item : widget_list) {
    PopAndFinalizeWidgetItems(item->level, &rendering_stack);
    rendering_stack.push(item);
    nvgSave(context_);
    nvgTranslate(context_, item->origin.x, item->origin.y);
    nvgScale(context_, item->scale, item->scale);
    nvgIntersectScissor(context_, 0, 0, item->width, item->height);
    item->widget->WidgetWillRender(context_);
    nvgSave(context_);
    item->widget->RenderOnDemand(context_);
    nvgRestore(context_);
  }
  PopAndFinalizeWidgetItems(0, &rendering_stack);
  nvgEndFrame(context_);

  // Notifies all attached widgets that the rendering process is done.
  WidgetViewDidRender(root_widget_);
}

void WidgetView::SetBounds(const int x, const int y, const int width,
                           const int height) {
  NativeView::SetBounds(x, y, width, height);
  root_widget_->SetWidth(Widget::Unit::kPoint, width);
  root_widget_->SetHeight(Widget::Unit::kPoint, height);
}

// Starts with root_widget_ to find the event responder recursively.
bool WidgetView::ShouldHandleEvent(const Point location) {
  return ShouldHandleEvent(location, root_widget_);
}

// Iterates children widgets of the specified widget in reversed order to find
// the event responder recursively.
bool WidgetView::ShouldHandleEvent(const Point location, Widget* widget) {
  if (widget->IsHidden())
    return false;

  for (auto it = widget->children().rbegin();
       it != widget->children().rend(); it++) {
    Widget* child = reinterpret_cast<Widget*>(*it);
    if (ShouldHandleEvent(location, child)) {
      return true;  // responder is set by one of its child
    }
    if (child->ShouldHandleEvent(location)) {
      event_responder_ = child;
      return true;
    }
  }
  return false;
}

void WidgetView::WidgetViewDidRender(Widget* widget) {
  nvgSave(context_);
  if (widget == root_widget_)
    ViewDidRender(context_);
  else
    widget->WidgetViewDidRender(context_);
  nvgRestore(context_);
  for (Widget* child : widget->children())
    WidgetViewDidRender(child);
}

void WidgetView::WidgetViewWillRender(Widget* widget) {
  nvgSave(context_);
  if (widget == root_widget_)
    ViewWillRender(context_);
  else
    widget->WidgetViewWillRender(context_);
  nvgRestore(context_);
  for (Widget* child : widget->children())
    WidgetViewWillRender(child);
}

void WidgetView::set_is_opaque(const bool is_opaque) {
  is_opaque_ = is_opaque;
  root_widget_->set_is_opaque(is_opaque);
}

}  // namespace moui
