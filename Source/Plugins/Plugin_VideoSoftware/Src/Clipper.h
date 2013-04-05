// Copyright (C) 2003-2009 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#ifndef _CLIPPER_H_
#define _CLIPPER_H_


#include "Common.h"
#include "SWNativeVertexFormat.h"
#include "ChunkFile.h"


namespace Clipper
{
    void Init();

    void SetViewOffset();

    void ProcessTriangle(OutputVertexData *v0, OutputVertexData *v1, OutputVertexData *v2);

	void ProcessLine(OutputVertexData *v0, OutputVertexData *v1);

    bool CullTest(OutputVertexData *v0, OutputVertexData *v1, OutputVertexData *v2, bool &backface);

    void PerspectiveDivide(OutputVertexData *vertex);

	void DoState(PointerWrap &p);
}


#endif
