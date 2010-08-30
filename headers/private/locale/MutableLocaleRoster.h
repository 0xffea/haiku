/*
 * Copyright 2010, Haiku. All rights reserved.
 * Distributed under the terms of the MIT license.
 */
#ifndef _MUTABLE_LOCALE_ROSTER_H_
#define _MUTABLE_LOCALE_ROSTER_H_


#include <Collator.h>
#include <Country.h>
#include <image.h>
#include <Language.h>
#include <List.h>
#include <Locale.h>
#include <Locker.h>
#include <LocaleRoster.h>
#include <Message.h>
#include <TimeZone.h>


class BLocale;
class BCatalog;
class BCatalogAddOn;

struct entry_ref;


namespace BPrivate {


class MutableLocaleRoster : public BLocaleRoster {
public:
								MutableLocaleRoster();
								~MutableLocaleRoster();

			status_t			GetDefaultLocale(BLocale* locale) const;

			status_t			SetDefaultCountry(const BCountry& country);
			status_t			SetDefaultLocale(const BLocale& locale);
			status_t			SetDefaultTimeZone(const BTimeZone& zone);

			status_t			SetPreferredLanguages(const BMessage* message);
									// the message contains one or more
									// 'language'-string-fields which
									// contain the language-name(s)

			status_t			GetSystemCatalog(BCatalogAddOn** catalog) const;

			BCatalogAddOn*		LoadCatalog(const char* signature,
									const char* language = NULL,
									int32 fingerprint = 0) const;
			BCatalogAddOn*		LoadEmbeddedCatalog(entry_ref* appOrAddOnRef);
			status_t			UnloadCatalog(BCatalogAddOn* addOn);

			BCatalogAddOn*		CreateCatalog(const char* type,
									const char* signature,
									const char* language);
};


extern MutableLocaleRoster* gMutableLocaleRoster;


typedef BCatalogAddOn* (*InstantiateCatalogFunc)(const char* name,
	const char* language, uint32 fingerprint);

typedef BCatalogAddOn* (*CreateCatalogFunc)(const char* name,
	const char* language);

typedef BCatalogAddOn* (*InstantiateEmbeddedCatalogFunc)(
	entry_ref* appOrAddOnRef);

typedef status_t (*GetAvailableLanguagesFunc)(BMessage*, const char*,
	const char*, int32);

/*
 * info about a single catalog-add-on (representing a catalog type):
 */
struct CatalogAddOnInfo {
			InstantiateCatalogFunc 			fInstantiateFunc;
			InstantiateEmbeddedCatalogFunc	fInstantiateEmbeddedFunc;
			CreateCatalogFunc				fCreateFunc;
			GetAvailableLanguagesFunc 		fLanguagesFunc;

			BString				fName;
			BString				fPath;
			image_id			fAddOnImage;
			uint8				fPriority;
			BList				fLoadedCatalogs;
			bool				fIsEmbedded;
									// an embedded add-on actually isn't an
									// add-on, it is included as part of the
									// library.
									// The DefaultCatalog is such a beast!

								CatalogAddOnInfo(const BString& name,
									const BString& path, uint8 priority);
								~CatalogAddOnInfo();

			bool				MakeSureItsLoaded();
			void				UnloadIfPossible();
};


/*
 * The global data that is shared between all roster-objects of a process.
 */
struct RosterData {
			BLocker				fLock;
			BList				fCatalogAddOnInfos;
			BMessage			fPreferredLanguages;

			BLocale				fDefaultLocale;
			BTimeZone			fDefaultTimeZone;

								RosterData();
								~RosterData();

			void				InitializeCatalogAddOns();
			void				CleanupCatalogAddOns();
			status_t			Refresh();

	static	int					CompareInfos(const void* left,
									const void* right);

			status_t			SetDefaultCountry(const BCountry& country);
			status_t			SetDefaultLocale(const BLocale& locale);
			status_t			SetDefaultTimeZone(const BTimeZone& zone);
			status_t			SetPreferredLanguages(const BMessage* msg);
private:
			status_t			_LoadLocaleSettings();
			status_t			_SaveLocaleSettings();

			status_t			_LoadTimeSettings();
			status_t			_SaveTimeSettings();

			status_t			_SetDefaultCountry(const BCountry& country);
			status_t			_SetDefaultLocale(const BLocale& locale);
			status_t			_SetDefaultTimeZone(const BTimeZone& zone);
			status_t			_SetPreferredLanguages(const BMessage* msg);

			status_t			_AddDefaultCountryToMessage(
									BMessage* message) const;
			status_t			_AddDefaultTimeZoneToMessage(
									BMessage* message) const;
			status_t			_AddPreferredLanguagesToMessage(
									BMessage* message) const;
};


extern RosterData gRosterData;


}	// namespace BPrivate


#endif	// _MUTABLE_LOCALE_ROSTER_H_
