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

#include "moui/ui/base_view.h"

#include "jni.h"
#include "moui/core/application.h"

namespace moui {

NativeView::NativeView(void* native_handle) {
  if (native_handle == nullptr)
    return;

  Application* application = Application::SharedApplication();
  JNIEnv* env = application->GetJNIEnv();
  native_handle_ = env->NewGlobalRef(reinterpret_cast<jobject>(native_handle));
}

NativeView::~NativeView() {
  if (native_handle_ == nullptr)
    return;

  Application* application = Application::SharedApplication();
  JNIEnv* env = application->GetJNIEnv();
  env->DeleteGlobalRef(reinterpret_cast<jobject>(native_handle_));
}

void NativeView::AddSubview(const NativeView* subview) {
  jobject native_view = reinterpret_cast<jobject>(native_handle_);
  jobject native_subview = reinterpret_cast<jobject>(subview->native_handle());

  Application* application = Application::SharedApplication();
  JNIEnv* env = application->GetJNIEnv();

  // Skips if the native view is not a subclass of ViewGroup.
  jclass group_view_class = env->FindClass("android/view/ViewGroup");
  if (!env->IsInstanceOf(native_view, group_view_class))
    return;

  jmethodID add_view_method = env->GetMethodID(
      group_view_class, "addView", "(Landroid/view/View;)V");
  env->CallVoidMethod(native_view, add_view_method, native_subview);
}

}  // namespace moui