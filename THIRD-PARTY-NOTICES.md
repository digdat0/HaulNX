# Third-party notices

TicoDL+ is distributed with, or links against, the following third-party
components. Their license notices are reproduced here as required.

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
This license is available with a FAQ at: https://openfontlicense.org

Permission is hereby granted, free of charge, to any person obtaining a copy
of the Font Software, to use, study, copy, merge, embed, modify, redistribute,
and sell modified and unmodified copies of the Font Software, subject to the
conditions of the SIL Open Font License, Version 1.1. The Font Software may
not be sold by itself; any redistribution must include this copyright notice
and license. The fonts, including any derivative works, may be bundled,
embedded, redistributed and/or sold with any software provided that any
reserved names are not used by derivative works.

THE FONT SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
```

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
