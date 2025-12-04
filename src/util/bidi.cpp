/*
Copyright (C) 2025 Dawid Gan <deveee@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation; either version 3.0 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "util/bidi.h"

#include <SheenBidi.h>

core::ustring applyBidiReordering(const core::ustring& text)
{
	if (text.empty())
		return text;
	
	SBCodepointSequence codepointSequence;
	codepointSequence.stringEncoding = SBStringEncodingUTF16;
	codepointSequence.stringBuffer = (void*)text.c_str();
	codepointSequence.stringLength = text.size();
	
	SBAlgorithmRef bidiAlgorithm = SBAlgorithmCreate(&codepointSequence);
	
	if (!bidiAlgorithm)
		return text;
	
	SBParagraphRef paragraph = SBAlgorithmCreateParagraph(bidiAlgorithm, 0, 
			text.size(), SBLevelDefaultLTR);
	
	if (!paragraph) {
		SBAlgorithmRelease(bidiAlgorithm);
		return text;
	}
	
	SBLineRef line = SBParagraphCreateLine(paragraph, 0, text.size());

	if (!line) {
		SBParagraphRelease(paragraph);
		SBAlgorithmRelease(bidiAlgorithm);
		return text;
	}
	
	SBUInteger runCount = SBLineGetRunCount(line);
	const SBRun *runsPtr = SBLineGetRunsPtr(line);
	
	core::ustring result;
	result.reserve(text.size());
	
	for (SBUInteger i = 0; i < runCount; i++) {
		const SBRun *run = runsPtr + i;
		bool isRTL = (run->level & 1) != 0;
		
		if (isRTL) {
			for (SBInteger j = run->length - 1; j >= 0; j--) {
				SBUInteger index = run->offset + j;
				result += text[index];
			}
		} else {
			for (SBUInteger j = 0; j < run->length; j++) {
				SBUInteger index = run->offset + j;
				result += text[index];
			}
		}
	}
	
	SBLineRelease(line);
	SBParagraphRelease(paragraph);
	SBAlgorithmRelease(bidiAlgorithm);
	
	return result;
}
