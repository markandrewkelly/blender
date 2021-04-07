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

#include <stdlib.h>
#include <string.h>

#include "bvh/bvh2.h"

#include "device/device.h"

#include "device/cpu/device.h"
#include "device/cuda/device.h"
#include "device/dummy/device.h"
#include "device/multi/device.h"
#include "device/opencl/device.h"
#include "device/optix/device.h"

#include "util/util_foreach.h"
#include "util/util_half.h"
#include "util/util_logging.h"
#include "util/util_math.h"
#include "util/util_opengl.h"
#include "util/util_string.h"
#include "util/util_system.h"
#include "util/util_time.h"
#include "util/util_types.h"
#include "util/util_vector.h"

CCL_NAMESPACE_BEGIN

bool Device::need_types_update = true;
bool Device::need_devices_update = true;
thread_mutex Device::device_mutex;
vector<DeviceInfo> Device::opencl_devices;
vector<DeviceInfo> Device::cuda_devices;
vector<DeviceInfo> Device::optix_devices;
vector<DeviceInfo> Device::cpu_devices;
uint Device::devices_initialized_mask = 0;

/* Device Requested Features */

std::ostream &operator<<(std::ostream &os, const DeviceRequestedFeatures &requested_features)
{
  os << "Experimental features: " << (requested_features.experimental ? "On" : "Off") << std::endl;
  os << "Max nodes group: " << requested_features.max_nodes_group << std::endl;
  /* TODO(sergey): Decode bitflag into list of names. */
  os << "Nodes features: " << requested_features.nodes_features << std::endl;
  os << "Use Hair: " << string_from_bool(requested_features.use_hair) << std::endl;
  os << "Use Object Motion: " << string_from_bool(requested_features.use_object_motion)
     << std::endl;
  os << "Use Camera Motion: " << string_from_bool(requested_features.use_camera_motion)
     << std::endl;
  os << "Use Baking: " << string_from_bool(requested_features.use_baking) << std::endl;
  os << "Use Subsurface: " << string_from_bool(requested_features.use_subsurface) << std::endl;
  os << "Use Volume: " << string_from_bool(requested_features.use_volume) << std::endl;
  os << "Use Patch Evaluation: " << string_from_bool(requested_features.use_patch_evaluation)
     << std::endl;
  os << "Use Transparent Shadows: " << string_from_bool(requested_features.use_transparent)
     << std::endl;
  os << "Use Principled BSDF: " << string_from_bool(requested_features.use_principled)
     << std::endl;
  os << "Use Denoising: " << string_from_bool(requested_features.use_denoising) << std::endl;
  os << "Use Displacement: " << string_from_bool(requested_features.use_true_displacement)
     << std::endl;
  os << "Use Background Light: " << string_from_bool(requested_features.use_background_light)
     << std::endl;
  return os;
}

/* Device */

Device::~Device() noexcept(false)
{
}

void Device::build_bvh(BVH *bvh, Progress &progress, bool refit)
{
  assert(bvh->params.bvh_layout == BVH_LAYOUT_BVH2);

  BVH2 *const bvh2 = static_cast<BVH2 *>(bvh);
  if (refit) {
    bvh2->refit(progress);
  }
  else {
    bvh2->build(progress, &stats);
  }
}

Device *Device::create(const DeviceInfo &info, Stats &stats, Profiler &profiler, bool background)
{
#ifdef WITH_MULTI
  if (!info.multi_devices.empty()) {
    /* Always create a multi device when info contains multiple devices.
     * This is done so that the type can still be e.g. DEVICE_CPU to indicate
     * that it is a homogeneous collection of devices, which simplifies checks. */
    return device_multi_create(info, stats, profiler, background);
  }
#endif

  Device *device = NULL;

  switch (info.type) {
    case DEVICE_CPU:
      device = device_cpu_create(info, stats, profiler, background);
      break;
#ifdef WITH_CUDA
    case DEVICE_CUDA:
      if (device_cuda_init())
        device = device_cuda_create(info, stats, profiler, background);
      break;
#endif
#ifdef WITH_OPTIX
    case DEVICE_OPTIX:
      if (device_optix_init())
        device = device_optix_create(info, stats, profiler, background);
      break;
#endif
#ifdef WITH_OPENCL
    case DEVICE_OPENCL:
      if (device_opencl_init())
        device = device_opencl_create(info, stats, profiler, background);
      break;
#endif
    default:
      break;
  }

  if (device == NULL) {
    device = device_dummy_create(info, stats, profiler, background);
  }

  return device;
}

DeviceType Device::type_from_string(const char *name)
{
  if (strcmp(name, "CPU") == 0)
    return DEVICE_CPU;
  else if (strcmp(name, "CUDA") == 0)
    return DEVICE_CUDA;
  else if (strcmp(name, "OPTIX") == 0)
    return DEVICE_OPTIX;
  else if (strcmp(name, "OPENCL") == 0)
    return DEVICE_OPENCL;
  else if (strcmp(name, "MULTI") == 0)
    return DEVICE_MULTI;

  return DEVICE_NONE;
}

string Device::string_from_type(DeviceType type)
{
  if (type == DEVICE_CPU)
    return "CPU";
  else if (type == DEVICE_CUDA)
    return "CUDA";
  else if (type == DEVICE_OPTIX)
    return "OPTIX";
  else if (type == DEVICE_OPENCL)
    return "OPENCL";
  else if (type == DEVICE_MULTI)
    return "MULTI";

  return "";
}

vector<DeviceType> Device::available_types()
{
  vector<DeviceType> types;
  types.push_back(DEVICE_CPU);
#ifdef WITH_CUDA
  types.push_back(DEVICE_CUDA);
#endif
#ifdef WITH_OPTIX
  types.push_back(DEVICE_OPTIX);
#endif
#ifdef WITH_OPENCL
  types.push_back(DEVICE_OPENCL);
#endif
  return types;
}

vector<DeviceInfo> Device::available_devices(uint mask)
{
  /* Lazy initialize devices. On some platforms OpenCL or CUDA drivers can
   * be broken and cause crashes when only trying to get device info, so
   * we don't want to do any initialization until the user chooses to. */
  thread_scoped_lock lock(device_mutex);
  vector<DeviceInfo> devices;

#ifdef WITH_OPENCL
  if (mask & DEVICE_MASK_OPENCL) {
    if (!(devices_initialized_mask & DEVICE_MASK_OPENCL)) {
      if (device_opencl_init()) {
        device_opencl_info(opencl_devices);
      }
      devices_initialized_mask |= DEVICE_MASK_OPENCL;
    }
    foreach (DeviceInfo &info, opencl_devices) {
      devices.push_back(info);
    }
  }
#endif

#if defined(WITH_CUDA) || defined(WITH_OPTIX)
  if (mask & (DEVICE_MASK_CUDA | DEVICE_MASK_OPTIX)) {
    if (!(devices_initialized_mask & DEVICE_MASK_CUDA)) {
      if (device_cuda_init()) {
        device_cuda_info(cuda_devices);
      }
      devices_initialized_mask |= DEVICE_MASK_CUDA;
    }
    if (mask & DEVICE_MASK_CUDA) {
      foreach (DeviceInfo &info, cuda_devices) {
        devices.push_back(info);
      }
    }
  }
#endif

#ifdef WITH_OPTIX
  if (mask & DEVICE_MASK_OPTIX) {
    if (!(devices_initialized_mask & DEVICE_MASK_OPTIX)) {
      if (device_optix_init()) {
        device_optix_info(cuda_devices, optix_devices);
      }
      devices_initialized_mask |= DEVICE_MASK_OPTIX;
    }
    foreach (DeviceInfo &info, optix_devices) {
      devices.push_back(info);
    }
  }
#endif

  if (mask & DEVICE_MASK_CPU) {
    if (!(devices_initialized_mask & DEVICE_MASK_CPU)) {
      device_cpu_info(cpu_devices);
      devices_initialized_mask |= DEVICE_MASK_CPU;
    }
    foreach (DeviceInfo &info, cpu_devices) {
      devices.push_back(info);
    }
  }

  return devices;
}

DeviceInfo Device::dummy_device(const string &error_msg)
{
  DeviceInfo info;
  info.type = DEVICE_DUMMY;
  info.error_msg = error_msg;
  return info;
}

string Device::device_capabilities(uint mask)
{
  thread_scoped_lock lock(device_mutex);
  string capabilities = "";

  if (mask & DEVICE_MASK_CPU) {
    capabilities += "\nCPU device capabilities: ";
    capabilities += device_cpu_capabilities() + "\n";
  }

#ifdef WITH_OPENCL
  if (mask & DEVICE_MASK_OPENCL) {
    if (device_opencl_init()) {
      capabilities += "\nOpenCL device capabilities:\n";
      capabilities += device_opencl_capabilities();
    }
  }
#endif

#ifdef WITH_CUDA
  if (mask & DEVICE_MASK_CUDA) {
    if (device_cuda_init()) {
      capabilities += "\nCUDA device capabilities:\n";
      capabilities += device_cuda_capabilities();
    }
  }
#endif

  return capabilities;
}

DeviceInfo Device::get_multi_device(const vector<DeviceInfo> &subdevices,
                                    int threads,
                                    bool background)
{
  assert(subdevices.size() > 0);

  if (subdevices.size() == 1) {
    /* No multi device needed. */
    return subdevices.front();
  }

  DeviceInfo info;
  info.type = subdevices.front().type;
  info.id = "MULTI";
  info.description = "Multi Device";
  info.num = 0;

  info.has_half_images = true;
  info.has_volume_decoupled = true;
  info.has_adaptive_stop_per_sample = true;
  info.has_osl = true;
  info.has_profiling = true;
  info.has_peer_memory = false;
  info.denoisers = DENOISER_ALL;

  foreach (const DeviceInfo &device, subdevices) {
    /* Ensure CPU device does not slow down GPU. */
    if (device.type == DEVICE_CPU && subdevices.size() > 1) {
      if (background) {
        int orig_cpu_threads = (threads) ? threads : system_cpu_thread_count();
        int cpu_threads = max(orig_cpu_threads - (subdevices.size() - 1), 0);

        VLOG(1) << "CPU render threads reduced from " << orig_cpu_threads << " to " << cpu_threads
                << ", to dedicate to GPU.";

        if (cpu_threads >= 1) {
          DeviceInfo cpu_device = device;
          cpu_device.cpu_threads = cpu_threads;
          info.multi_devices.push_back(cpu_device);
        }
        else {
          continue;
        }
      }
      else {
        VLOG(1) << "CPU render threads disabled for interactive render.";
        continue;
      }
    }
    else {
      info.multi_devices.push_back(device);
    }

    /* Create unique ID for this combination of devices. */
    info.id += device.id;

    /* Set device type to MULTI if subdevices are not of a common type. */
    if (device.type != info.type) {
      info.type = DEVICE_MULTI;
    }

    /* Accumulate device info. */
    info.has_half_images &= device.has_half_images;
    info.has_volume_decoupled &= device.has_volume_decoupled;
    info.has_adaptive_stop_per_sample &= device.has_adaptive_stop_per_sample;
    info.has_osl &= device.has_osl;
    info.has_profiling &= device.has_profiling;
    info.has_peer_memory |= device.has_peer_memory;
    info.denoisers &= device.denoisers;
  }

  return info;
}

void Device::tag_update()
{
  free_memory();
}

void Device::free_memory()
{
  devices_initialized_mask = 0;
  cuda_devices.free_memory();
  optix_devices.free_memory();
  opencl_devices.free_memory();
  cpu_devices.free_memory();
}

/* DeviceInfo */

void DeviceInfo::add_denoising_devices(DenoiserType denoiser_type)
{
  assert(denoising_devices.empty());

  if (denoiser_type == DENOISER_OPTIX && type != DEVICE_OPTIX) {
    vector<DeviceInfo> optix_devices = Device::available_devices(DEVICE_MASK_OPTIX);
    if (!optix_devices.empty()) {
      /* Convert to a special multi device with separate denoising devices. */
      if (multi_devices.empty()) {
        multi_devices.push_back(*this);
      }

      /* Try to use the same physical devices for denoising. */
      for (const DeviceInfo &cuda_device : multi_devices) {
        if (cuda_device.type == DEVICE_CUDA) {
          for (const DeviceInfo &optix_device : optix_devices) {
            if (cuda_device.num == optix_device.num) {
              id += optix_device.id;
              denoising_devices.push_back(optix_device);
              break;
            }
          }
        }
      }

      if (denoising_devices.empty()) {
        /* Simply use the first available OptiX device. */
        const DeviceInfo optix_device = optix_devices.front();
        id += optix_device.id; /* Uniquely identify this special multi device. */
        denoising_devices.push_back(optix_device);
      }

      denoisers = denoiser_type;
    }
  }
  else if (denoiser_type == DENOISER_OPENIMAGEDENOISE && type != DEVICE_CPU) {
    /* Convert to a special multi device with separate denoising devices. */
    if (multi_devices.empty()) {
      multi_devices.push_back(*this);
    }

    /* Add CPU denoising devices. */
    DeviceInfo cpu_device = Device::available_devices(DEVICE_MASK_CPU).front();
    denoising_devices.push_back(cpu_device);

    denoisers = denoiser_type;
  }
}

CCL_NAMESPACE_END
