/*
** Copyright 2010, Oliver Tappe, zooey@hirschkaefer.de. All rights reserved.
** Distributed under the terms of the Haiku License.
*/

#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <wctype.h>

#include "LocaleBackend.h"


using BPrivate::Libroot::gLocaleBackend;

/*
 * In many of the following functions, we make use of the fact that with
 * gLocaleBackend == NULL, the POSIX locale is active. Since the POSIX locale
 * only contains chars 0-127 and those ASCII chars are compatible with the
 * UTF32 values used in wint_t, we can delegate to the respective ctype
 * function.
 */

int
iswalnum(wint_t wc)
{
	if (gLocaleBackend == NULL)
		return __isctype(wc, _ISalnum);

	return gLocaleBackend->IsWCType(wc, _ISalnum);
}


int
iswalpha(wint_t wc)
{
	if (gLocaleBackend == NULL)
		return __isctype(wc, _ISalpha);

	return gLocaleBackend->IsWCType(wc, _ISalpha);
}


int
iswblank(wint_t wc)
{
	if (gLocaleBackend == NULL)
		return __isctype(wc, _ISblank);

	return gLocaleBackend->IsWCType(wc, _ISblank);
}


int
iswcntrl(wint_t wc)
{
	if (gLocaleBackend == NULL)
		return __isctype(wc, _IScntrl);

	return gLocaleBackend->IsWCType(wc, _IScntrl);
}


int
iswdigit(wint_t wc)
{
	if (gLocaleBackend == NULL)
		return __isctype(wc, _ISdigit);

	return gLocaleBackend->IsWCType(wc, _ISdigit);
}


int
iswgraph(wint_t wc)
{
	if (gLocaleBackend == NULL)
		return __isctype(wc, _ISgraph);

	return gLocaleBackend->IsWCType(wc, _ISgraph);
}


int
iswlower(wint_t wc)
{
	if (gLocaleBackend == NULL)
		return __isctype(wc, _ISlower);

	return gLocaleBackend->IsWCType(wc, _ISlower);
}


int
iswprint(wint_t wc)
{
	if (gLocaleBackend == NULL)
		return __isctype(wc, _ISprint);

	return gLocaleBackend->IsWCType(wc, _ISprint);
}


int
iswpunct(wint_t wc)
{
	if (gLocaleBackend == NULL)
		return __isctype(wc, _ISpunct);

	return gLocaleBackend->IsWCType(wc, _ISpunct);
}


int
iswspace(wint_t wc)
{
	if (gLocaleBackend == NULL)
		return __isctype(wc, _ISspace);

	return gLocaleBackend->IsWCType(wc, _ISspace);
}


int
iswupper(wint_t wc)
{
	if (gLocaleBackend == NULL)
		return __isctype(wc, _ISupper);

	return gLocaleBackend->IsWCType(wc, _ISupper);
}


int
iswxdigit(wint_t wc)
{
	if (gLocaleBackend == NULL)
		return __isctype(wc, _ISxdigit);

	return gLocaleBackend->IsWCType(wc, _ISxdigit);
}


int
iswctype(wint_t wc, wctype_t charClass)
{
	if (gLocaleBackend == NULL)
		return __isctype(wc, charClass);

	return gLocaleBackend->IsWCType(wc, charClass);
}


wint_t
towlower(wint_t wc)
{
	if (gLocaleBackend == NULL)
		return tolower(wc);

	wint_t result = 0;
	gLocaleBackend->ToWCTrans(wc, _ISlower, result);

	return result;
}


wint_t
towupper(wint_t wc)
{
	if (gLocaleBackend == NULL)
		return toupper(wc);

	wint_t result = 0;
	gLocaleBackend->ToWCTrans(wc, _ISupper, result);

	return result;
}


wint_t
towctrans(wint_t wc, wctrans_t transition)
{
	if (gLocaleBackend == NULL)
		return tolower(wc);

	wint_t result = 0;
	status_t status = gLocaleBackend->ToWCTrans(wc, transition, result);

	if (status != B_OK)
		errno = EINVAL;

	return result;
}


wctrans_t
wctrans(const char *charClass)
{
	if (charClass != NULL) {
		// we do not know any locale-specific character classes
		if (strcmp(charClass, "tolower") == 0)
			return _ISlower;
		if (strcmp(charClass, "toupper") == 0)
			return _ISupper;
	}

	errno = EINVAL;
	return 0;
}


wctype_t
wctype(const char *property)
{
	// currently, we do not support any locale-specific properties
	if (strcmp(property, "alnum") == 0)
		return _ISalnum;
	if (strcmp(property, "alpha") == 0)
		return _ISalpha;
	if (strcmp(property, "blank") == 0)
		return _ISblank;
	if (strcmp(property, "cntrl") == 0)
		return _IScntrl;
	if (strcmp(property, "digit") == 0)
		return _ISdigit;
	if (strcmp(property, "graph") == 0)
		return _ISgraph;
	if (strcmp(property, "lower") == 0)
		return _ISlower;
	if (strcmp(property, "print") == 0)
		return _ISprint;
	if (strcmp(property, "punct") == 0)
		return _ISpunct;
	if (strcmp(property, "space") == 0)
		return _ISspace;
	if (strcmp(property, "upper") == 0)
		return _ISupper;
	if (strcmp(property, "xdigit") == 0)
		return _ISxdigit;

	return 0;
}
