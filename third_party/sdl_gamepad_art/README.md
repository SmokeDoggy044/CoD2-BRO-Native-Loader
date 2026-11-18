# SDL gamepad art

`gamepad_front.h`, `gamepad_back.h`, `gamepad_face_abxy.h`, `gamepad_face_bayx.h`, `gamepad_face_sony.h` and
`gamepad_button.h` are PNG images as C arrays, copied unchanged from SDL 3.4.18's `test/` directory
(https://github.com/libsdl-org/SDL, the testcontroller tool's art). The launcher's controller mapper
(`src/launcher/pad_mapper.cpp`) draws them.

License (SDL `test/LICENSE.txt`):

    Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

    This software is provided 'as-is', without any express or implied
    warranty.  In no event will the authors be held liable for any damages
    arising from the use of this software.

    Permission is granted to anyone to use this software for any purpose,
    including commercial applications, and to alter it and redistribute it
    freely.
