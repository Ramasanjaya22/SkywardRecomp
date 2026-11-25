/*
 * xxh3_128.h -- XXH3 128-bit, seed 0, standard default secret.
 *
 * Copyright (c) Kenji-NX Team and Contributors
 * Licensed under the MIT License.
 *
 * C99 port of Ryujinx.Common.XXHash128 (KenjiNX). KenjiNX prints the
 * texture-pack hash as high64 then low64, each 16 lowercase hex digits:
 *
 *     Hash128 hash = XXHash128.ComputeHash(data);
 *     return $"{hash.High:x16}{hash.Low:x16}";
 *
 * Original C# source (MIT):
 * https://git.ryujinx.app/projects/Kenji-NX/raw/branch/master/src/Ryujinx.Common/XXHash128.cs
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#ifndef RUNTIME_NVN_XXH3_128_H
#define RUNTIME_NVN_XXH3_128_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Compute the KenjiNX XXH3-128 texture hash of data[0..len).
 * out[0] = high 64 bits, out[1] = low 64 bits, in KenjiNX print order.
 * len may be 0 (data may then be NULL).
 */
void xxh3_128(const void *data, size_t len, uint64_t out[2]);

#ifdef __cplusplus
}
#endif

#endif /* RUNTIME_NVN_XXH3_128_H */
