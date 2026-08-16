# Third-party notices

HaulNX is distributed with, or links against, the following third-party
components. Their license notices are reproduced here as required.

HaulNX as a whole is licensed under the GNU General Public License, version 3
or later (see [LICENSE](LICENSE) and
[`licenses/GPL-3.0.txt`](licenses/GPL-3.0.txt)), because it incorporates
LGPLv3 code (unarr) and follows the pattern of an Apache-2.0 reference
(cmtp-responder), both below. The other components listed here are under
GPL-compatible licenses.

---

## unarr

The native RAR3 filter-decoder fallback under `source/rar3/` and
`include/rar3.h` ports the relevant slice of **unarr**
(https://github.com/zeniko/unarr, upstream commit `d1be8c43a8`) — the base
RAR3 archive/header parsing, Huffman and LZSS+range decompression, and the
fingerprint-matched native filter functions (delta/E8/RGB/audio), with the
general-purpose filter bytecode interpreter deliberately removed (see
`include/rar3.h` for why). Ported files retain unarr's own file-level
copyright header.

unarr is licensed under the **GNU Lesser General Public License, version 3**.
Because that code is incorporated into and distributed as part of HaulNX, the
combined work is covered by the GPLv3 (LGPLv3 §4 permits this: conveying the
combination under the GPL). The full license text is bundled at
[`licenses/LGPL-3.0.txt`](licenses/LGPL-3.0.txt).

```
Copyright 2015 the unarr project authors (see AUTHORS file).
License: LGPLv3
```

unarr's own `AUTHORS` file additionally credits The Unarchiver project
(https://code.google.com/p/theunarchiver/) and Simon Bünzli, and notes that
`common/crc32.c` and the `lzmasdk/` sources it depends on (both carried
forward unmodified here) are Public Domain.

---

## cmtp-responder

The embedded USB MTP responder's protocol/command layer under `include/mtp/`
and `source/mtp/` follows the container-framing and per-operation dispatch
pattern used by **cmtp-responder**
(https://github.com/cmtp-responder/cmtp-responder) — a permissively-licensed,
platform-generic MTP responder (forked from Tizen's `mtp-responder` by
Collabora specifically to depend only on generic libraries). Its protocol
layer is cleanly separated from its own Linux FunctionFS transport, which is
what makes it usable as a layout reference without pulling in anything
platform-specific; the USB transport in `source/mtp/usb_session.cpp` here is
written fresh against libnx's own `usbds` API instead.

cmtp-responder is licensed under the **Apache License, Version 2.0**.

```
Copyright (c) 2012, 2013 Samsung Electronics Co., Ltd.
Copyright (c) 2019 Collabora Ltd

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
```

The full license text is bundled at
[`licenses/Apache-2.0.txt`](licenses/Apache-2.0.txt).

---

## Plutonium

Graphical UI library — https://github.com/XorTroll/Plutonium

```
MIT License

Copyright (c) 2018-2019 XorTroll

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## Noto Sans (subset)

`romfs/fonts/viet-fallback.ttf` is a modified glyph subset of **Noto Sans
Regular** (Latin Extended Additional + combining diacritics, used as a
fallback for Vietnamese text) — https://notofonts.github.io/. Modifications:
subset to the listed ranges, vertical metrics matched to the console's system
font for baseline alignment, and renamed to "Viet Fallback Sans" per the OFL's
Reserved Font Name rule.

```
Copyright 2022 The Noto Project Authors
(https://github.com/notofonts/latin-greek-cyrillic)

This Font Software is licensed under the SIL Open Font License, Version 1.1.
```

The **complete** SIL Open Font License 1.1 text (as required by the license)
is bundled at [`licenses/OFL-1.1.txt`](licenses/OFL-1.1.txt).

---

## jsmn

JSON tokenizer (vendored) — https://github.com/zserge/jsmn

```
MIT License

Copyright (c) 2010 Serge Zaitsev

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

Other build/runtime dependencies (devkitPro / libnx, libcurl, libarchive, SDL2
and its codec libraries) are provided via the devkitPro toolchain under their
own respective licenses.
