/*
 * Copyright 2008, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */

#include "BasicTerminalBuffer.h"

#include <stdlib.h>
#include <string.h>

#include <algorithm>

#include <String.h>

#include "CodeConv.h"
#include "TermConst.h"
#include "TerminalCharClassifier.h"


static const UTF8Char kSpaceChar(' ');


static inline int32
restrict_value(int32 value, int32 min, int32 max)
{
	return value < min ? min : (value > max ? max : value);
}


// #pragma mark - private inline methods


inline int32
BasicTerminalBuffer::_LineIndex(int32 index) const
{
	return (index + fScreenOffset) % fHistoryCapacity;
}


inline BasicTerminalBuffer::Line*
BasicTerminalBuffer::_LineAt(int32 index) const
{
	return fHistory[_LineIndex(index)];
}


inline BasicTerminalBuffer::Line*
BasicTerminalBuffer::_HistoryLineAt(int32 index) const
{
	if (index >= fHeight || index < -fHistorySize)
		return NULL;

	return _LineAt(index + fHistoryCapacity);
}


inline void
BasicTerminalBuffer::_Invalidate(int32 top, int32 bottom)
{
//debug_printf("%p->BasicTerminalBuffer::_Invalidate(%ld, %ld)\n", this, top, bottom);
	fDirtyInfo.ExtendDirtyRegion(top, bottom);

	if (!fDirtyInfo.messageSent) {
		NotifyListener();
		fDirtyInfo.messageSent = true;
	}
}


inline void
BasicTerminalBuffer::_CursorChanged()
{
	if (!fDirtyInfo.messageSent) {
		NotifyListener();
		fDirtyInfo.messageSent = true;
	}
}


// #pragma mark - public methods


BasicTerminalBuffer::BasicTerminalBuffer()
	:
	fHistory(NULL)
{
}


BasicTerminalBuffer::~BasicTerminalBuffer()
{
	_FreeLines(fHistory, fHistoryCapacity);
}


status_t
BasicTerminalBuffer::Init(int32 width, int32 height, int32 historySize)
{
	if (historySize < 2 * height)
		historySize = 2 * height;

	fWidth = width;
	fHeight = height;

	fScrollTop = 0;
	fScrollBottom = fHeight - 1;

	fCursor.x = 0;
	fCursor.y = 0;

	fOverwriteMode = true;

	fHistoryCapacity = historySize;
	fHistorySize = 0;

	fHistory = _AllocateLines(width, historySize);
	if (fHistory == NULL)
		return B_NO_MEMORY;

	_ClearLines(0, fHeight - 1);

	fDirtyInfo.Reset();

	return B_OK;
}


status_t
BasicTerminalBuffer::ResizeTo(int32 width, int32 height)
{
	return ResizeTo(width, height, fHistoryCapacity);
}


status_t
BasicTerminalBuffer::ResizeTo(int32 width, int32 height, int32 historyCapacity)
{
	if (height < MIN_ROWS || height > MAX_ROWS || width < MIN_COLS
			|| width > MAX_COLS || height > historyCapacity) {
		return B_BAD_VALUE;
	}

//debug_printf("BasicTerminalBuffer::ResizeTo(): (%ld, %ld, history: %ld) -> "
//"(%ld, %ld, history: %ld)\n", fWidth, fHeight, fHistoryCapacity, width, height,
//historyCapacity);

	if (width != fWidth) {
		// The width changes. We have to allocate a new line array and re-wrap
		// all lines.
		Line** history = _AllocateLines(width, historyCapacity);
		if (history == NULL)
			return B_NO_MEMORY;

		int32 totalLines = fHistorySize + fHeight;
		int32 historyOffset = fHistoryCapacity - fHistorySize;
			// base for _LineAt() invocations to iterate through the history

		// re-wrap
		TermPos cursor;
		int32 destIndex = 0;
		int32 sourceIndex = 0;
		int32 sourceX = 0;
		int32 destTotalLines = 0;
		int32 destScreenOffset = 0;
		int32 maxDestTotalLines = INT_MAX;
		bool newDestLine = true;
		while (sourceIndex < totalLines) {
			Line* sourceLine = _LineAt(historyOffset + sourceIndex);
			Line* destLine = history[destIndex];

			if (newDestLine) {
				destLine->Clear();
				newDestLine = false;
			}

			int32 sourceLeft = sourceLine->length - sourceX;
			int32 destLeft = width - destLine->length;
//debug_printf("    source: %ld, left: %ld, dest: %ld, left: %ld\n",
//sourceIndex, sourceLeft, destIndex, destLeft);

			if (sourceIndex == fHistorySize && sourceX == 0) {
				destScreenOffset = destTotalLines;
				if (destLeft == 0 && sourceLeft > 0)
					destScreenOffset++;
//debug_printf("      destScreenOffset: %ld\n", destScreenOffset);
			}

			int32 toCopy = min_c(sourceLeft, destLeft);
			// If the last cell to copy is the first cell of a
			// full-width char, don't copy it yet.
			if (toCopy > 0 && IS_WIDTH(
					sourceLine->cells[sourceX + toCopy - 1].attributes)) {
//debug_printf("      -> last char is full-width -- don't copy it\n");
				toCopy--;
			}

			// translate the cursor position
			if (fCursor.y + fHistorySize == sourceIndex
				&& fCursor.x >= sourceX
				&& (fCursor.x < sourceX + toCopy
					|| destLeft >= sourceLeft
						&& sourceX + sourceLeft <= fCursor.x)) {
				cursor.x = destLine->length + fCursor.x - sourceX;
				cursor.y = destTotalLines;

				if (cursor.x >= width) {
					// The cursor was in free space after the official end
					// of line.
					cursor.x = width - 1;
				}
//debug_printf("      cursor: (%ld, %ld)\n", cursor.x, cursor.y);

				// don't allow the cursor to get out of screen
				maxDestTotalLines = cursor.y + height - 1;
			}

			if (toCopy > 0) {
				memcpy(destLine->cells + destLine->length,
					sourceLine->cells + sourceX, toCopy * sizeof(Cell));
				destLine->length += toCopy;
			}

			bool nextDestLine = false;
			if (toCopy == sourceLeft) {
				if (!sourceLine->softBreak)
					nextDestLine = true;
				sourceIndex++;
				sourceX = 0;
			} else {
				destLine->softBreak = true;
				nextDestLine = true;
				sourceX += toCopy;
			}

			if (nextDestLine) {
				destIndex = (destIndex + 1) % historyCapacity;
				destTotalLines++;
				newDestLine = true;
				if (destTotalLines >= maxDestTotalLines)
					break;
			}
		}

		// If the last source line had a soft break, the last dest line
		// won't have been counted yet.
		if (!newDestLine) {
			destIndex = (destIndex + 1) % historyCapacity;
			destTotalLines++;
		}

//debug_printf("  total lines: %ld -> %ld\n", totalLines, destTotalLines);

		int32 tempHeight = destTotalLines - destScreenOffset;
		cursor.y -= destScreenOffset;

		// Re-wrapping might have produced more lines than we have room for.
		if (destTotalLines > historyCapacity)
			destTotalLines = historyCapacity;

		// Update the values
//debug_printf("  cursor: (%ld, %ld) -> (%ld, %ld)\n", fCursor.x, fCursor.y,
//cursor.x, cursor.y);
		fCursor.x = cursor.x;
		fCursor.y = cursor.y;
//debug_printf("  screen offset: %ld -> %ld\n", fScreenOffset,
//destScreenOffset % fHistoryCapacity);
		fScreenOffset = destScreenOffset % historyCapacity;
//debug_printf("  history size: %ld -> %ld\n", fHistorySize, destTotalLines - fHeight);
		fHistorySize = destTotalLines - tempHeight;
//debug_printf("  height %ld -> %ld\n", fHeight, tempHeight);
		fHeight = tempHeight;
		fWidth = width;

		_FreeLines(fHistory, fHistoryCapacity);
		fHistory = history;
		fHistoryCapacity = historyCapacity;
	}

	if (historyCapacity > fHistoryCapacity)
		SetHistoryCapacity(historyCapacity);

	if (height == fHeight)
		return B_OK;

	// The height changes. We just add/remove lines at the end of the screen.

	if (height < fHeight) {
		// The screen shrinks. We just drop the lines at the end of the screen,
		// but we keep the cursor on screen at all costs.
		if (fCursor.y >= height) {
			int32 toShift = fCursor.y - height + 1;
			fScreenOffset = (fScreenOffset + fHistoryCapacity + toShift)
				% fHistoryCapacity;
			fHistorySize += toShift;
			fCursor.y -= toShift;
		}
	} else {
		// The screen grows. We add empty lines at the end of the current
		// screen.
		if (fHistorySize + height > fHistoryCapacity)
			fHistorySize = fHistoryCapacity - height;

		for (int32 i = fHeight; i < height; i++)
			_LineAt(i)->Clear();
	}

//debug_printf("  cursor: -> (%ld, %ld)\n", fCursor.x, fCursor.y);
//debug_printf("  screen offset: -> %ld\n", fScreenOffset);
//debug_printf("  history size: -> %ld\n", fHistorySize);

	fHeight = height;

	// reset scroll range to keep it simple
	fScrollTop = 0;
	fScrollBottom = fHeight - 1;

	if (historyCapacity < fHistoryCapacity)
		SetHistoryCapacity(historyCapacity);

	return B_OK;
}


status_t
BasicTerminalBuffer::SetHistoryCapacity(int32 historyCapacity)
{
	if (historyCapacity < fHeight)
		return B_BAD_VALUE;

	if (fHistoryCapacity == historyCapacity)
		return B_OK;

	// The history capacity changes.
	Line** history = _AllocateLines(fWidth, historyCapacity);
	if (history == NULL)
		return B_NO_MEMORY;

	int32 totalLines = fHistorySize + fHeight;
	int32 historyOffset = fHistoryCapacity - fHistorySize;
		// base for _LineAt() invocations to iterate through the history

	if (totalLines > historyCapacity) {
		// Our new history capacity is smaller than currently stored line,
		// so we have to drop lines.
		historyOffset += totalLines - historyCapacity;
		totalLines = historyCapacity;
	}

	for (int32 i = 0; i < totalLines; i++) {
		Line* sourceLine = _LineAt(historyOffset + i);
		Line* destLine = history[i];
		destLine->length = sourceLine->length;
		destLine->softBreak = sourceLine->softBreak;
		if (destLine->length > 0) {
			memcpy(destLine->cells, sourceLine->cells,
				destLine->length * sizeof(Cell));
		}
	}

	_FreeLines(fHistory, fHistoryCapacity);
	fHistory = history;
	fHistoryCapacity = historyCapacity;
	fHistorySize = totalLines - fHeight;
	fScreenOffset = fHistorySize;

	return B_OK;
}


void
BasicTerminalBuffer::Clear()
{
	fHistorySize = 0;
	fScreenOffset = 0;

	_ClearLines(0, fHeight - 1);

	fCursor.SetTo(0, 0);

	fDirtyInfo.linesScrolled = 0;
	_Invalidate(0, fHeight - 1);
}


void
BasicTerminalBuffer::SynchronizeWith(const BasicTerminalBuffer* other,
	int32 offset, int32 dirtyTop, int32 dirtyBottom)
{
//debug_printf("BasicTerminalBuffer::SynchronizeWith(%p, %ld, %ld - %ld)\n",
//other, offset, dirtyTop, dirtyBottom);

	// intersect the visible region with the dirty region
	int32 first = 0;
	int32 last = fHeight - 1;
	dirtyTop -= offset;
	dirtyBottom -= offset;

	if (first > dirtyBottom || dirtyTop > last)
		return;

	if (first < dirtyTop)
		first = dirtyTop;
	if (last > dirtyBottom)
		last = dirtyBottom;

	// update the dirty lines
//debug_printf("  updating: %ld - %ld\n", first, last);
	for (int32 i = first; i <= last; i++) {
		Line* sourceLine = other->_HistoryLineAt(i + offset);
		Line* destLine = _LineAt(i);
		if (sourceLine != NULL) {
			destLine->length = sourceLine->length;
			destLine->softBreak = sourceLine->softBreak;
			if (destLine->length > 0) {
				memcpy(destLine->cells, sourceLine->cells,
					destLine->length * sizeof(Cell));
			}
		} else
			destLine->Clear();
	}
}


bool
BasicTerminalBuffer::IsFullWidthChar(int32 row, int32 column) const
{
	Line* line = _HistoryLineAt(row);
	return line != NULL && column > 0 && column < line->length
		&& (line->cells[column - 1].attributes & A_WIDTH) != 0;
}


int
BasicTerminalBuffer::GetChar(int32 row, int32 column, UTF8Char& character,
	uint16& attributes) const
{
	Line* line = _HistoryLineAt(row);
	if (line == NULL)
		return NO_CHAR;

	if (column < 0 || column >= line->length)
		return NO_CHAR;

	if (column > 0 && (line->cells[column - 1].attributes & A_WIDTH) != 0)
		return IN_STRING;

	Cell& cell = line->cells[column];
	character = cell.character;
	attributes = cell.attributes;
	return A_CHAR;
}


int32
BasicTerminalBuffer::GetString(int32 row, int32 firstColumn, int32 lastColumn,
	char* buffer, uint16& attributes) const
{
	Line* line = _HistoryLineAt(row);
	if (line == NULL)
		return 0;

	if (lastColumn >= line->length)
		lastColumn = line->length - 1;

	int32 column = firstColumn;
	if (column <= lastColumn)
		attributes = line->cells[column].attributes;

	for (; column <= lastColumn; column++) {
		Cell& cell = line->cells[column];
		if (cell.attributes != attributes)
			break;

		int32 bytes = cell.character.ByteCount();
		for (int32 i = 0; i < bytes; i++)
			*buffer++ = cell.character.bytes[i];
	}

	*buffer = '\0';

	return column - firstColumn;
}


void
BasicTerminalBuffer::GetStringFromRegion(BString& string, const TermPos& start,
	const TermPos& end) const
{
//debug_printf("BasicTerminalBuffer::GetStringFromRegion((%ld, %ld), (%ld, %ld))\n",
//start.x, start.y, end.x, end.y);
	if (start >= end)
		return;

	TermPos pos(start);

	if (IsFullWidthChar(pos.y, pos.x))
		pos.x--;

	// get all but the last line
	while (pos.y < end.y) {
		if (_GetPartialLineString(string, pos.y, pos.x, fWidth))
			string.Append('\n', 1);
		pos.x = 0;
		pos.y++;
	}

	// get the last line, if not empty
	if (end.x > 0)
		_GetPartialLineString(string, end.y, pos.x, end.x);
}


bool
BasicTerminalBuffer::FindWord(const TermPos& pos,
	TerminalCharClassifier* classifier, bool findNonWords, TermPos& _start,
	TermPos& _end) const
{
	int32 x = pos.x;
	int32 y = pos.y;

	Line* line = _HistoryLineAt(y);
	if (line == NULL || x < 0 || x >= fWidth)
		return false;

	if (x >= line->length) {
		// beyond the end of the line -- select all space
		if (!findNonWords)
			return false;

		_start.SetTo(line->length, y);
		_end.SetTo(fWidth, y);
		return true;
	}

	if (x > 0 && IS_WIDTH(line->cells[x - 1].attributes))
		x--;

	// get the char type at the given position
	int type = classifier->Classify(line->cells[x].character.bytes);

	// check whether we are supposed to find words only
	if (type != CHAR_TYPE_WORD_CHAR && !findNonWords)
		return false;

	// find the beginning
	TermPos start(x, y);
	TermPos end(x + (IS_WIDTH(line->cells[x].attributes) ? 2 : 1), y);
	while (true) {
		if (--x < 0) {
			// Hit the beginning of the line -- continue at the end of the
			// previous line, if it soft-breaks.
			y--;
			if ((line = _HistoryLineAt(y)) == NULL || !line->softBreak
					|| line->length == 0) {
				break;
			}
			x = line->length - 1;
		}
		if (x > 0 && IS_WIDTH(line->cells[x - 1].attributes))
			x--;

		if (classifier->Classify(line->cells[x].character.bytes) != type)
			break;

		start.SetTo(x, y);
	}

	// find the end
	x = end.x;
	y = end.y;
	line = _HistoryLineAt(y);

	while (true) {
		if (x >= line->length) {
			// Hit the end of the line -- if it soft-breaks continue with the
			// next line.
			if (!line->softBreak)
				break;
			y++;
			x = 0;
			if ((line = _HistoryLineAt(y)) == NULL)
				break;
		}

		if (classifier->Classify(line->cells[x].character.bytes) != type)
			break;

		x += IS_WIDTH(line->cells[x].attributes) ? 2 : 1;
		end.SetTo(x, y);
	}

	_start = start;
	_end = end;
	return true;
}


int32
BasicTerminalBuffer::LineLength(int32 index) const
{
	Line* line = _HistoryLineAt(index);
	return line != NULL ? line->length : 0;
}


bool
BasicTerminalBuffer::Find(const char* _pattern, const TermPos& start,
	bool forward, bool caseSensitive, bool matchWord, TermPos& _matchStart,
	TermPos& _matchEnd) const
{
//debug_printf("BasicTerminalBuffer::Find(\"%s\", (%ld, %ld), forward: %d, case: %d, "
//"word: %d)\n", _pattern, start.x, start.y, forward, caseSensitive, matchWord);
	// normalize pos, so that _NextChar() and _PreviousChar() are happy
	TermPos pos(start);
	Line* line = _HistoryLineAt(pos.y);
	if (line != NULL) {
		if (forward) {
			while (line != NULL && pos.x >= line->length && line->softBreak) {
				pos.x = 0;
				pos.y++;
				line = _HistoryLineAt(pos.y);
			}
		} else {
			if (pos.x > line->length)
				pos.x = line->length;
		}
	}

	int32 patternByteLen = strlen(_pattern);

	// convert pattern to UTF8Char array
	UTF8Char pattern[patternByteLen];
	int32 patternLen = 0;
	while (*_pattern != '\0') {
		int32 charLen = UTF8Char::ByteCount(*_pattern);
		if (charLen > 0) {
			pattern[patternLen].SetTo(_pattern, charLen);

			// if not case sensitive, convert to lower case
			if (!caseSensitive && charLen == 1)
				pattern[patternLen] = pattern[patternLen].ToLower();

			patternLen++;
			_pattern += charLen;
		} else
			_pattern++;
	}
//debug_printf("  pattern byte len: %ld, pattern len: %ld\n", patternByteLen, patternLen);

	if (patternLen == 0)
		return false;

	// reverse pattern, if searching backward
	if (!forward) {
		for (int32 i = 0; i < patternLen / 2; i++)
			std::swap(pattern[i], pattern[patternLen - i - 1]);
	}

	// search loop
	int32 matchIndex = 0;
	TermPos matchStart;
	while (true) {
//debug_printf("    (%ld, %ld): matchIndex: %ld\n", pos.x, pos.y, matchIndex);
		TermPos previousPos(pos);
		UTF8Char c;
		if (!(forward ? _NextChar(pos, c) : _PreviousChar(pos, c)))
			return false;

		if (caseSensitive ? (c == pattern[matchIndex])
				: (c.ToLower() == pattern[matchIndex])) {
			if (matchIndex == 0)
				matchStart = previousPos;

			matchIndex++;

			if (matchIndex == patternLen) {
//debug_printf("      match!\n");
				// compute the match range
				TermPos matchEnd(pos);
				if (!forward)
					std::swap(matchStart, matchEnd);

				// check word match
				if (matchWord) {
					TermPos tempPos(matchStart);
					if (_PreviousChar(tempPos, c) && !c.IsSpace()
						|| _NextChar(tempPos = matchEnd, c) && !c.IsSpace()) {
//debug_printf("      but no word match!\n");
						continue;
					}
				}

				_matchStart = matchStart;
				_matchEnd = matchEnd;
//debug_printf("  -> (%ld, %ld) - (%ld, %ld)\n", matchStart.x, matchStart.y,
//matchEnd.x, matchEnd.y);
				return true;
			}
		} else if (matchIndex > 0) {
			// continue after the position where we started matching
			pos = matchStart;
			if (forward)
				_NextChar(pos, c);
			else
				_PreviousChar(pos, c);
			matchIndex = 0;
		}
	}
}


void
BasicTerminalBuffer::InsertChar(UTF8Char c, uint32 attributes)
{
//debug_printf("BasicTerminalBuffer::InsertChar('%.*s' (%d), %#lx)\n",
//(int)c.ByteCount(), c.bytes, c.bytes[0], attributes);
	int width = CodeConv::UTF8GetFontWidth(c.bytes);
	if (width == FULL_WIDTH)
		attributes |= A_WIDTH;

	if (fCursor.x + width > fWidth)
		_SoftBreakLine();
	else
		_PadLineToCursor();

	if (!fOverwriteMode)
		_InsertGap(width);

	Line* line = _LineAt(fCursor.y);
	line->cells[fCursor.x].character = c;
	line->cells[fCursor.x].attributes = attributes;

	if (line->length < fCursor.x + width)
		line->length = fCursor.x + width;

	_Invalidate(fCursor.y, fCursor.y);

	fCursor.x += width;

// TODO: Deal correctly with full-width chars! We must take care not to
// overwrite half of a full-width char. This holds also for other methods.

	if (fCursor.x == fWidth)
		_SoftBreakLine();
			// TODO: Handle a subsequent CR correctly!
}


void
BasicTerminalBuffer::InsertCR()
{
	_LineAt(fCursor.y)->softBreak = false;
	fCursor.x = 0;
	_CursorChanged();
}


void
BasicTerminalBuffer::InsertLF()
{
	// If we're at the end of the scroll region, scroll. Otherwise just advance
	// the cursor.
	if (fCursor.y == fScrollBottom) {
		_Scroll(fScrollTop, fScrollBottom, 1);
	} else {
		fCursor.y++;
		_CursorChanged();
	}
}


void
BasicTerminalBuffer::InsertLines(int32 numLines)
{
	if (fCursor.y >= fScrollTop && fCursor.y < fScrollBottom)
		_Scroll(fCursor.y, fScrollBottom, -numLines);
}


void
BasicTerminalBuffer::SetInsertMode(int flag)
{
	fOverwriteMode = flag == MODE_OVER;
}


void
BasicTerminalBuffer::InsertSpace(int32 num)
{
// TODO: Deal with full-width chars!
	if (fCursor.x + num > fWidth)
		num = fWidth - fCursor.x;

	if (num > 0) {
		_PadLineToCursor();
		_InsertGap(num);

		Line* line = _LineAt(fCursor.y);
		for (int32 i = fCursor.x; i < fCursor.x + num; i++) {
			line->cells[i].character = kSpaceChar;
			line->cells[i].attributes = 0;
		}
	}
}


void
BasicTerminalBuffer::EraseBelow()
{
	_Scroll(fCursor.y, fHeight - 1, fHeight);
}


void
BasicTerminalBuffer::DeleteChars(int32 numChars)
{
	Line* line = _LineAt(fCursor.y);
	if (fCursor.x < line->length) {
		if (fCursor.x + numChars < line->length) {
			int32 left = line->length - fCursor.x - numChars;
			memmove(line->cells + fCursor.x, line->cells + fCursor.x + numChars,
				left * sizeof(Cell));
			line->length = fCursor.x + left;
		} else {
			// remove all remaining chars
			line->length = fCursor.x;
		}

		_Invalidate(fCursor.y, fCursor.y);
	}
}


void
BasicTerminalBuffer::DeleteColumns()
{
	Line* line = _LineAt(fCursor.y);
	if (fCursor.x < line->length) {
		line->length = fCursor.x;
		_Invalidate(fCursor.y, fCursor.y);
	}
}


void
BasicTerminalBuffer::DeleteLines(int32 numLines)
{
	if (fCursor.y >= fScrollTop && fCursor.y <= fScrollBottom)
		_Scroll(fCursor.y, fScrollBottom, numLines);
}


void
BasicTerminalBuffer::SetCursor(int32 x, int32 y)
{
//debug_printf("BasicTerminalBuffer::SetCursor(%d, %d)\n", x, y);
	x = restrict_value(x, 0, fWidth - 1);
	y = restrict_value(y, fScrollTop, fScrollBottom);
	if (x != fCursor.x || y != fCursor.y) {
		fCursor.x = x;
		fCursor.y = y;
		_CursorChanged();
	}
}


void
BasicTerminalBuffer::SaveCursor()
{
	fSavedCursor = fCursor;
}


void
BasicTerminalBuffer::RestoreCursor()
{
	SetCursor(fSavedCursor.x, fSavedCursor.y);
}


void
BasicTerminalBuffer::SetScrollRegion(int32 top, int32 bottom)
{
	fScrollTop = restrict_value(top, 0, fHeight - 1);
	fScrollBottom = restrict_value(bottom, fScrollTop, fHeight - 1);

	// also sets the cursor position
	SetCursor(0, 0);
}


void
BasicTerminalBuffer::NotifyListener()
{
	// Implemented by derived classes.
}


// #pragma mark - private methods


/* static */ BasicTerminalBuffer::Line**
BasicTerminalBuffer::_AllocateLines(int32 width, int32 count)
{
	Line** lines = (Line**)malloc(sizeof(Line*) * count);
	if (lines == NULL)
		return NULL;

	for (int32 i = 0; i < count; i++) {
		lines[i] = (Line*)malloc(sizeof(Line) + sizeof(Cell) * (width - 1));
		if (lines[i] == NULL) {
			_FreeLines(lines, i);
			return NULL;
		}
	}

	return lines;
}


/* static */ void
BasicTerminalBuffer::_FreeLines(Line** lines, int32 count)
{
	if (lines != NULL) {
		for (int32 i = 0; i < count; i++)
			free(lines[i]);

		free(lines);
	}
}


void
BasicTerminalBuffer::_ClearLines(int32 first, int32 last)
{
	int32 firstCleared = -1;
	int32 lastCleared = -1;

	for (int32 i = first; i <= last; i++) {
		Line* line = _LineAt(i);
		if (line->length > 0) {
			if (firstCleared == -1)
				firstCleared = i;
			lastCleared = i;

			line->Clear();
		}
	}

	if (firstCleared >= 0)
		_Invalidate(firstCleared, lastCleared);
}


void
BasicTerminalBuffer::_Scroll(int32 top, int32 bottom, int32 numLines)
{
	if (numLines == 0)
		return;

	if (numLines > 0) {
		// scroll text up
		if (top == 0) {
			// The lines scrolled out of the screen range are transferred to
			// the history.

			if (numLines > fHistoryCapacity)
				numLines = fHistoryCapacity;

			// make room for numLines new lines
			if (fHistorySize + fHeight + numLines > fHistoryCapacity) {
				int32 toDrop = fHistorySize + fHeight + numLines
					- fHistoryCapacity;
				fHistorySize -= toDrop;
					// Note that fHistorySize can temporarily become negative,
					// but all will be well again, when we offset the screen.
			}

			// clear numLines after the current screen
			for (int32 i = fHeight; i < fHeight + numLines; i++)
				_LineAt(i)->Clear();

			if (bottom < fHeight - 1) {
				// Only scroll part of the screen. Move the unscrolled lines to
				// their new location.
				for (int32 i = bottom + 1; i < fHeight; i++) {
					std::swap(fHistory[_LineIndex(i)],
						fHistory[_LineIndex(i) + numLines]);
				}
			}

			// all lines are in place -- offset the screen
			fScreenOffset = (fScreenOffset + numLines) % fHistoryCapacity;
			fHistorySize += numLines;

			// scroll/extend dirty range

			if (fDirtyInfo.dirtyTop != INT_MAX) {
				// If the top or bottom of the dirty region are above the
				// bottom of the scroll region, we have to scroll them up.
				if (fDirtyInfo.dirtyTop <= bottom) {
					fDirtyInfo.dirtyTop -= numLines;
					if (fDirtyInfo.dirtyBottom <= bottom)
						fDirtyInfo.dirtyBottom -= numLines;
				}

				// numLines above the bottom become dirty
				_Invalidate(bottom - numLines + 1, bottom);
			}

			fDirtyInfo.linesScrolled += numLines;
// TODO: The linesScrolled might be suboptimal when scrolling partially
// only, since we would scroll the whole visible area, including unscrolled
// lines, which invalidates them, too.

			// invalidate new empty lines
			_Invalidate(bottom + 1 - numLines, bottom);

		} else if (numLines >= bottom - top + 1) {
			// all lines are completely scrolled out of range -- just clear
			// them
			_ClearLines(top, bottom);
		} else {
			// partial scroll -- clear the lines scrolled out of range and move
			// the other ones
			for (int32 i = top + numLines; i <= bottom; i++) {
				int32 lineToDrop = _LineIndex(i - numLines);
				int32 lineToKeep = _LineIndex(i);
				fHistory[lineToDrop]->Clear();
				std::swap(fHistory[lineToDrop], fHistory[lineToKeep]);
			}

			_Invalidate(top, bottom);
		}
	} else {
		// scroll text down
		numLines = -numLines;

		if (numLines >= bottom - top + 1) {
			// all lines are completely scrolled out of range -- just clear
			// them
			_ClearLines(top, bottom);
		} else {
			// partial scroll -- clear the lines scrolled out of range and move
			// the other ones
			for (int32 i = bottom - numLines; i >= top; i--) {
				int32 lineToKeep = _LineIndex(i);
				int32 lineToDrop = _LineIndex(i + numLines);
				fHistory[lineToDrop]->Clear();
				std::swap(fHistory[lineToDrop], fHistory[lineToKeep]);
			}

			_Invalidate(top, bottom);
		}
	}
}


void
BasicTerminalBuffer::_SoftBreakLine()
{
	Line* line = _LineAt(fCursor.y);
	line->length = fCursor.x;
	line->softBreak = true;

	fCursor.x = 0;
	if (fCursor.y == fScrollBottom)
		_Scroll(fScrollTop, fScrollBottom, 1);
	else
		fCursor.y++;
}


void
BasicTerminalBuffer::_PadLineToCursor()
{
	Line* line = _LineAt(fCursor.y);
	if (line->length < fCursor.x) {
		for (int32 i = line->length; i < fCursor.x; i++) {
			line->cells[i].character = kSpaceChar;
			line->cells[i].attributes = 0;
				// TODO: Other attributes?
		}
	}
}


void
BasicTerminalBuffer::_InsertGap(int32 width)
{
// ASSERT(fCursor.x + width <= fWidth)
	Line* line = _LineAt(fCursor.y);

	int32 toMove = min_c(line->length - fCursor.x, fWidth - fCursor.x - width);
	if (toMove > 0) {
		memmove(line->cells + fCursor.x + width,
			line->cells + fCursor.x, toMove * sizeof(Cell));
	}

	line->length += width;
}


/*!	\a endColumn is not inclusive.
*/
bool
BasicTerminalBuffer::_GetPartialLineString(BString& string, int32 row,
	int32 startColumn, int32 endColumn) const
{
	Line* line = _HistoryLineAt(row);
	if (line == NULL)
		return false;

	if (endColumn > line->length)
		endColumn = line->length;

	for (int32 x = startColumn; x < endColumn; x++) {
		const Cell& cell = line->cells[x];
		string.Append(cell.character.bytes, cell.character.ByteCount());

		if (IS_WIDTH(cell.attributes))
			x++;
	}

	return true;
}


/*!	Decrement \a pos and return the char at that location.
*/
bool
BasicTerminalBuffer::_PreviousChar(TermPos& pos, UTF8Char& c) const
{
	pos.x--;

	Line* line = _HistoryLineAt(pos.y);

	while (true) {
		if (pos.x < 0) {
			pos.y--;
			line = _HistoryLineAt(pos.y);
			if (line == NULL)
				return false;

			pos.x = line->length;
			if (line->softBreak) {
				pos.x--;
			} else {
				c = '\n';
				return true;
			}
		} else {
			c = line->cells[pos.x].character;
			return true;
		}
	}
}


/*!	Return the char at \a pos and increment it.
*/
bool
BasicTerminalBuffer::_NextChar(TermPos& pos, UTF8Char& c) const
{
	Line* line = _HistoryLineAt(pos.y);
	if (line == NULL)
		return false;

	if (pos.x >= line->length) {
		c = '\n';
		pos.x = 0;
		pos.y++;
		return true;
	}

	c = line->cells[pos.x].character;

	pos.x++;
	while (line != NULL && pos.x >= line->length && line->softBreak) {
		pos.x = 0;
		pos.y++;
		line = _HistoryLineAt(pos.y);
	}

	return true;
}
