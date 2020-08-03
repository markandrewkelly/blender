
uniform sampler3D velocityX;
uniform sampler3D velocityY;
uniform sampler3D velocityZ;
uniform float displaySize = 1.0;
uniform float slicePosition;
uniform int sliceAxis; /* -1 is no slice, 0 is X, 1 is Y, 2 is Z. */
uniform bool scaleWithMagnitude = false;

/* FluidDomainSettings.cell_size */
uniform vec3 cellSize;
/* FluidDomainSettings.p0 */
uniform vec3 domainOriginOffset;
/* FluidDomainSettings.res_min */
uniform ivec3 adaptiveCellOffset;

#ifdef USE_MAC
out vec4 finalColor;
#else
flat out vec4 finalColor;
#endif

const vec3 corners[4] = vec3[4](vec3(0.0, 0.2, -0.5),
                                vec3(-0.2 * 0.866, -0.2 * 0.5, -0.5),
                                vec3(0.2 * 0.866, -0.2 * 0.5, -0.5),
                                vec3(0.0, 0.0, 0.5));

const int indices[12] = int[12](0, 1, 1, 2, 2, 0, 0, 3, 1, 3, 2, 3);

/* Straight Port from BKE_defvert_weight_to_rgb()
 * TODO port this to a color ramp. */
vec3 weight_to_color(float weight)
{
  vec3 r_rgb = vec3(0.0);
  float blend = ((weight / 2.0) + 0.5);

  if (weight <= 0.25) { /* blue->cyan */
    r_rgb.g = blend * weight * 4.0;
    r_rgb.b = blend;
  }
  else if (weight <= 0.50) { /* cyan->green */
    r_rgb.g = blend;
    r_rgb.b = blend * (1.0 - ((weight - 0.25) * 4.0));
  }
  else if (weight <= 0.75) { /* green->yellow */
    r_rgb.r = blend * ((weight - 0.50) * 4.0);
    r_rgb.g = blend;
  }
  else if (weight <= 1.0) { /* yellow->red */
    r_rgb.r = blend;
    r_rgb.g = blend * (1.0 - ((weight - 0.75) * 4.0));
  }
  else {
    /* exceptional value, unclamped or nan,
     * avoid uninitialized memory use */
    r_rgb = vec3(1.0, 0.0, 1.0);
  }

  return r_rgb;
}

mat3 rotation_from_vector(vec3 v)
{
  /* Add epsilon to avoid NaN. */
  vec3 N = normalize(v + 1e-8);
  vec3 UpVector = abs(N.z) < 0.99999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
  vec3 T = normalize(cross(UpVector, N));
  vec3 B = cross(N, T);
  return mat3(T, B, N);
}

void main()
{
#ifdef USE_NEEDLE
  int cell = gl_VertexID / 12;
#elif defined(USE_MAC)
  int cell = gl_VertexID / 6;
#else
  int cell = gl_VertexID / 2;
#endif

  ivec3 volume_size = textureSize(velocityX, 0);

  ivec3 cell_ofs = ivec3(0);
  ivec3 cell_div = volume_size;
  if (sliceAxis == 0) {
    cell_ofs.x = int(slicePosition * float(volume_size.x));
    cell_div.x = 1;
  }
  else if (sliceAxis == 1) {
    cell_ofs.y = int(slicePosition * float(volume_size.y));
    cell_div.y = 1;
  }
  else if (sliceAxis == 2) {
    cell_ofs.z = int(slicePosition * float(volume_size.z));
    cell_div.z = 1;
  }

  ivec3 cell_co;
  cell_co.x = cell % cell_div.x;
  cell_co.y = (cell / cell_div.x) % cell_div.y;
  cell_co.z = cell / (cell_div.x * cell_div.y);
  cell_co += cell_ofs;

  vec3 pos = domainOriginOffset + cellSize * (vec3(cell_co + adaptiveCellOffset) + 0.5);

  vec3 velocity;
  velocity.x = texelFetch(velocityX, cell_co, 0).r;
  velocity.y = texelFetch(velocityY, cell_co, 0).r;
  velocity.z = texelFetch(velocityZ, cell_co, 0).r;

#ifdef USE_MAC
  vec3 color;

  switch (gl_VertexID % 6) {
    case 0: /* tail of X component */
      pos.x += -0.5 * cellSize.x;
      color = vec3(1.0, 0.0, 0.0); /* red */
      break;
    case 1: /* head of X component */
      pos.x += (-0.5 + velocity.x * displaySize) * cellSize.x;
      color = vec3(1.0, 1.0, 0.0); /* yellow */
      break;
    case 2: /* tail of Y component */
      pos.y += -0.5 * cellSize.y;
      color = vec3(0.0, 1.0, 0.0); /* green */
      break;
    case 3: /* head of Y component */
      pos.y += (-0.5 + velocity.y * displaySize) * cellSize.y;
      color = vec3(1.0, 1.0, 0.0); /* yellow */
      break;
    case 4: /* tail of Z component */
      pos.z += -0.5 * cellSize.z;
      color = vec3(0.0, 0.0, 1.0); /* blue */
      break;
    case 5: /* head of Z component */
      pos.z += (-0.5 + velocity.z * displaySize) * cellSize.z;
      color = vec3(1.0, 1.0, 0.0); /* yellow */
      break;
  }

  finalColor = vec4(color, 1.0);
#else
  finalColor = vec4(weight_to_color(length(velocity)), 1.0);

  float vector_length = 1.0;

  if (scaleWithMagnitude) {
    vector_length = length(velocity);
  }
  else if (length(velocity) == 0.0) {
    vector_length = 0.0;
  }

  mat3 rot_mat = rotation_from_vector(velocity);

#  ifdef USE_NEEDLE
  vec3 rotated_pos = rot_mat * corners[indices[gl_VertexID % 12]];
  pos += rotated_pos * vector_length * displaySize * cellSize;
#  else
  vec3 rotated_pos = rot_mat * vec3(0.0, 0.0, 1.0);
  pos += ((gl_VertexID % 2) == 1) ? rotated_pos * vector_length * displaySize * cellSize :
                                    vec3(0.0);
#  endif
#endif

  vec3 world_pos = point_object_to_world(pos);
  gl_Position = point_world_to_ndc(world_pos);
}
