# Third-party notices

WordsWASM is distributed under the [MIT License](LICENSE). Its executable,
WebAssembly, npm, and database distributions also contain or are derived from
the components and data identified below. This file travels with every npm
package and release archive so recipients do not need a checkout of this
repository to find their provenance.

## William Whitaker's WORDS source code and lexical data

WordsWASM is a new C++ implementation. Its morphological behaviour and WWDB
datasets are derived from William A. Whitaker's WORDS program and data. The
Ada reference tree is used to build the datasets and as a compatibility oracle;
it is not the WebAssembly implementation. The source snapshots used by this
release are recorded in the repository at
[`whitakers-words/remotes.txt`](https://github.com/Fabio3rs/WordsWASM/blob/master/whitakers-words/remotes.txt).

The original WORDS notice, including its request for attribution, is:

> WORDS, a Latin dictionary, by Colonel William Whitaker (USAF, Retired)
>
> Copyright William A. Whitaker (1936-2010)
>
> This is a free program, which means it is proper to copy it and pass it on
> to your friends. Consider it a developmental item for which there is no
> charge. However, just for form, it is Copyrighted (c). Permission is hereby
> freely given for any and all use of program and data. You can sell it as your
> own, but at least tell me.
>
> This version is distributed without obligation, but the developer would
> appreciate comments and suggestions.
>
> All parts of the WORDS system, source code and data files, are made freely
> available to anyone who wishes to use them, for whatever purpose.

The complete source-tree notice is available at
[`whitakers-words/LICENCE.txt`](https://github.com/Fabio3rs/WordsWASM/blob/master/whitakers-words/LICENCE.txt).

## utf8proc and Unicode data

The default native CLI uses the full
[utf8proc](https://github.com/JuliaStrings/utf8proc) backend for UTF-8
validation and normalization. The default WebAssembly build does not link
utf8proc: it uses a finite backend for the Latin input alphabet accepted by
WordsWASM. That backend still decodes UTF-8 strictly, but it is not a general
Unicode normalization library. These notices are included in every
distribution to document the dependency and the provenance of the compact
backend's compatibility work. The vendored source snapshot is
[`0075ed7`](https://github.com/JuliaStrings/utf8proc/commit/0075ed7d0adba45682ee6bf7a83b10f8fd110163).
utf8proc is MIT licensed:

> Copyright (c) 2014-2021 Steven G. Johnson, Jiahao Chen, Tony Kelman, Jonas
> Fonseca, and other contributors listed in the git history.
>
> Permission is hereby granted, free of charge, to any person obtaining a copy
> of this software and associated documentation files (the "Software"), to
> deal in the Software without restriction, including without limitation the
> rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
> sell copies of the Software, and to permit persons to whom the Software is
> furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in
> all copies or substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
> FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
> AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
> LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
> FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
> IN THE SOFTWARE.

utf8proc also carries the original MIT notice:

> Copyright (c) 2009, 2013 Public Software Group e. V., Berlin, Germany
>
> Permission is hereby granted, free of charge, to any person obtaining a copy
> of this software and associated documentation files (the "Software"), to
> deal in the Software without restriction, including without limitation the
> rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
> sell copies of the Software, and to permit persons to whom the Software is
> furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in
> all copies or substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
> FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
> AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
> LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
> FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
> IN THE SOFTWARE.

Its Unicode data is subject to this Unicode, Inc. notice:

> Copyright (c) 1991-2007 Unicode, Inc. All rights reserved. Distributed under
> the Terms of Use in http://www.unicode.org/copyright.html.
>
> Permission is hereby granted, free of charge, to any person obtaining a copy
> of the Unicode data files and any associated documentation (the "Data Files")
> or Unicode software and any associated documentation (the "Software") to
> deal in the Data Files or Software without restriction, including without
> limitation the rights to use, copy, modify, merge, publish, distribute,
> and/or sell copies of the Data Files or Software, and to permit persons to
> whom the Data Files or Software are furnished to do so, provided that (a) the
> above copyright notice(s) and this permission notice appear with all copies
> of the Data Files or Software, (b) both the above copyright notice(s) and
> this permission notice appear in associated documentation, and (c) there is
> clear notice in each modified Data File or in the Software as well as in the
> documentation associated with the Data File(s) or Software that the data or
> software has been modified.
>
> THE DATA FILES AND SOFTWARE ARE PROVIDED "AS IS", WITHOUT WARRANTY OF ANY
> KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
> MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
> THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS
> INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT OR
> CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
> DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
> TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
> OF THE DATA FILES OR SOFTWARE.
>
> Except as contained in this notice, the name of a copyright holder shall not
> be used in advertising or otherwise to promote the sale, use or other
> dealings in these Data Files or Software without prior written authorization
> of the copyright holder.
>
> Unicode and the Unicode logo are trademarks of Unicode, Inc., and may be
> registered in some jurisdictions. All other trademarks and registered
> trademarks mentioned herein are the property of their respective owners.

The source utf8proc notice is also available in its
[`LICENSE.md`](https://github.com/JuliaStrings/utf8proc/blob/0075ed7d0adba45682ee6bf7a83b10f8fd110163/LICENSE.md).

## nlohmann/json

Native CLI builds include
[nlohmann/json](https://github.com/nlohmann/json) for JSON presentation. The
vendored source snapshot is
[`35705d7`](https://github.com/nlohmann/json/commit/35705d79d878db5ca1a282ec0f8243a80010d24e).

> MIT License
>
> Copyright (c) 2013-2026 Niels Lohmann
>
> Permission is hereby granted, free of charge, to any person obtaining a copy
> of this software and associated documentation files (the "Software"), to
> deal in the Software without restriction, including without limitation the
> rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
> sell copies of the Software, and to permit persons to whom the Software is
> furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in
> all copies or substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
> FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
> AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
> LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
> FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
> IN THE SOFTWARE.

The complete nlohmann/json repository includes further notices for components
used by its development and test tree; the native distributable uses the MIT
licensed JSON header. See
[`LICENSE.MIT`](https://github.com/nlohmann/json/blob/35705d79d878db5ca1a282ec0f8243a80010d24e/LICENSE.MIT).
