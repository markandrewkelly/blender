/*
 * Copyright 2011-2013 Blender Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "device/device_memory.h"

CCL_NAMESPACE_BEGIN

enum DenoiserType {
  DENOISER_OPTIX = 2,
  DENOISER_OPENIMAGEDENOISE = 4,
  DENOISER_NUM,

  DENOISER_NONE = 0,
  DENOISER_ALL = ~0,
};

enum DenoiserInput {
  DENOISER_INPUT_RGB = 1,
  DENOISER_INPUT_RGB_ALBEDO = 2,
  DENOISER_INPUT_RGB_ALBEDO_NORMAL = 3,

  DENOISER_INPUT_NUM,
};

typedef int DenoiserTypeMask;

class DenoiseParams {
 public:
  /* Apply denoiser to image. */
  bool use;

  /* Output denoising data passes (possibly without applying the denoiser). */
  bool store_passes;

  /* Denoiser type. */
  DenoiserType type;

  /* Viewport start sample. */
  int start_sample;

  /** OIDN/Optix Denoiser **/

  /* Passes handed over to the OIDN/OptiX denoiser (default to color + albedo). */
  DenoiserInput input_passes;

  DenoiseParams()
  {
    use = false;
    store_passes = false;

    type = DENOISER_OPENIMAGEDENOISE;

    /* Default to color + albedo only, since normal input does not always have the desired effect
     * when denoising with OptiX. */
    input_passes = DENOISER_INPUT_RGB_ALBEDO;

    start_sample = 0;
  }

  bool modified(const DenoiseParams &other) const
  {
    return !(use == other.use && store_passes == other.store_passes && type == other.type &&
             start_sample == other.start_sample && input_passes == other.input_passes);
  }
};

/* All the parameters needed to perform buffer denoising on a device.
 * Is not really a task in its canonical terms (as in, is not an asynchronous running task). Is
 * more like a wrapper for all the arguments and parameters needed to perform denoising. Is a
 * single place where they are all listed, so that it's not required to modify all device methods
 * when these parameters do change. */
class DeviceDenoiseTask {
 public:
  int x, y;
  int width, height;

  int offset, stride;

  int pass_stride;

  device_ptr buffer;

  int num_samples;

  int pass_denoising_color;
  int pass_denoising_normal;
  int pass_denoising_albedo;

  DenoiseParams params;
};

CCL_NAMESPACE_END
