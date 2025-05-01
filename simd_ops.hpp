#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

// Check if SIMD is available at compile time
#if defined(__wasm_simd128__)
#include <wasm_simd128.h>
#define HAS_WASM_SIMD 1
#else
#define HAS_WASM_SIMD 0
#endif

// Runtime detection of SIMD support
inline bool detectSIMDSupport() {
#if HAS_WASM_SIMD
  return true;
#else
  return EM_ASM_INT({
    return (typeof WebAssembly.Feature !=
            = 'undefined' && WebAssembly.Feature.hasOwnProperty('SIMD') &&
              WebAssembly.validate(new Uint8Array([
                0, 97, 115, 109, 1,  0, 0, 0, 1,  5, 1,   96, 0,   1,  123, 3,
                2, 1,  0,   10,  10, 1, 8, 0, 65, 0, 253, 15, 253, 98, 11
              ])));
  });
#endif
}

// SIMD-optimized 3D vector dot product
inline float simd_dot(const glm::vec3 &a, const glm::vec3 &b) {
#if HAS_WASM_SIMD
  // Load vectors into SIMD registers with 0 as fourth component
  v128_t va = wasm_v128_load(glm::value_ptr(a));
  v128_t vb = wasm_v128_load(glm::value_ptr(b));

  // Multiply vectors component-wise
  v128_t prod = wasm_f32x4_mul(va, vb);

  // Horizontal sum: add all components
  // First add first two with last two: (x+y, z+w, x+y, z+w)
  v128_t sum1 =
      wasm_f32x4_add(prod, wasm_v32x4_shuffle(prod, prod, 2, 3, 0, 1));
  // Then add the duplicated results: (x+y+z+w, ...)
  v128_t sum2 =
      wasm_f32x4_add(sum1, wasm_v32x4_shuffle(sum1, sum1, 1, 0, 3, 2));

  // Extract the first lane which contains the full sum
  return wasm_f32x4_extract_lane(sum2, 0);
#else
  // Fallback to regular dot product
  return glm::dot(a, b);
#endif
}

// SIMD-optimized 3D vector cross product
inline glm::vec3 simd_cross(const glm::vec3 &a, const glm::vec3 &b) {
#if HAS_WASM_SIMD
  // Load vectors into SIMD registers with 0 as fourth component
  v128_t va = wasm_v128_load(glm::value_ptr(a));
  v128_t vb = wasm_v128_load(glm::value_ptr(b));

  // Shuffle components to prepare for cross product
  v128_t va_yzx = wasm_v32x4_shuffle(va, va, 1, 2, 0, 3);
  v128_t vb_yzx = wasm_v32x4_shuffle(vb, vb, 1, 2, 0, 3);

  // Cross product implementation: (a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z,
  // a.x*b.y - a.y*b.x)
  v128_t prod1 = wasm_f32x4_mul(va, wasm_v32x4_shuffle(vb, vb, 2, 0, 1, 3));
  v128_t prod2 = wasm_f32x4_mul(va_yzx, vb);
  v128_t result = wasm_f32x4_sub(prod1, prod2);

  // Rearrange to get the correct output order
  result = wasm_v32x4_shuffle(result, result, 1, 2, 0, 3);

  // Convert back to glm::vec3
  glm::vec3 output;
  wasm_v128_store(glm::value_ptr(output), result);
  return output;
#else
  // Fallback to regular cross product
  return glm::cross(a, b);
#endif
}

// SIMD-optimized 4D vector-matrix multiplication
inline glm::vec4 simd_mat4_mul_vec4(const glm::mat4 &m, const glm::vec4 &v) {
#if HAS_WASM_SIMD
  // Load vector into SIMD register
  v128_t vv = wasm_v128_load(glm::value_ptr(v));

  // Load matrix rows into SIMD registers
  v128_t row0 = wasm_v128_load(glm::value_ptr(m[0]));
  v128_t row1 = wasm_v128_load(glm::value_ptr(m[1]));
  v128_t row2 = wasm_v128_load(glm::value_ptr(m[2]));
  v128_t row3 = wasm_v128_load(glm::value_ptr(m[3]));

  // Multiply and add operations
  // For each row, multiply by the vector and then sum all components
  v128_t result = wasm_f32x4_mul(row0, vv);
  result = wasm_f32x4_add(result, wasm_f32x4_mul(row1, vv));
  result = wasm_f32x4_add(result, wasm_f32x4_mul(row2, vv));
  result = wasm_f32x4_add(result, wasm_f32x4_mul(row3, vv));

  // Convert back to glm::vec4
  glm::vec4 output;
  wasm_v128_store(glm::value_ptr(output), result);
  return output;
#else
  // Fallback to regular matrix-vector multiplication
  return m * v;
#endif
}

// SIMD-optimized 3D vector normalization
inline glm::vec3 simd_normalize(const glm::vec3 &v) {
#if HAS_WASM_SIMD
  // Load vector into SIMD register with 0 as fourth component
  v128_t vv = wasm_v128_load(glm::value_ptr(v));

  // Compute squared length (dot product with itself)
  v128_t squared = wasm_f32x4_mul(vv, vv);

  // Sum all components
  v128_t sum1 =
      wasm_f32x4_add(squared, wasm_v32x4_shuffle(squared, squared, 2, 3, 0, 1));
  v128_t len_squared =
      wasm_f32x4_add(sum1, wasm_v32x4_shuffle(sum1, sum1, 1, 0, 3, 2));

  // Calculate length using sqrt
  v128_t len = wasm_f32x4_sqrt(len_squared);

  // Create a vector of 1.0 and divide to get inverse length
  v128_t one = wasm_f32x4_splat(1.0f);
  v128_t inv_len = wasm_f32x4_div(one, len);

  // Multiply by inverse length to normalize
  v128_t normalized = wasm_f32x4_mul(vv, inv_len);

  // Convert back to glm::vec3
  glm::vec3 output;
  wasm_v128_store(glm::value_ptr(output), normalized);
  return output;
#else
  // Fallback to regular normalization
  return glm::normalize(v);
#endif
}

#else
// Non-Emscripten builds use standard functions
inline float simd_dot(const glm::vec3 &a, const glm::vec3 &b) {
  return glm::dot(a, b);
}

inline glm::vec3 simd_cross(const glm::vec3 &a, const glm::vec3 &b) {
  return glm::cross(a, b);
}

inline glm::vec4 simd_mat4_mul_vec4(const glm::mat4 &m, const glm::vec4 &v) {
  return m * v;
}

inline glm::vec3 simd_normalize(const glm::vec3 &v) {
  return glm::normalize(v);
}

inline bool detectSIMDSupport() {
  return false; // Not supported in non-Emscripten builds
}
#endif
