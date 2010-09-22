/*
 * Copyright 2006 - 2010, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef	ALM_LAYOUT_H
#define	ALM_LAYOUT_H

#include <AbstractLayout.h>
#include <File.h>
#include <List.h>
#include <Size.h>
#include <SupportDefs.h>
#include <View.h>

#include "Area.h"
#include "Column.h"
#include "LayoutStyleType.h"
#include "LinearSpec.h"
#include "Row.h"
#include "XTab.h"
#include "YTab.h"


namespace BALM {
	
/**
 * A GUI layout engine using the ALM.
 */
class BALMLayout : public BAbstractLayout {
public:
								BALMLayout();
	virtual						~BALMLayout();

			void				SolveLayout();

			XTab*				AddXTab();
			YTab*				AddYTab();
			Row*				AddRow();
			Row*				AddRow(YTab* top, YTab* bottom);
			Column*				AddColumn();
			Column*				AddColumn(XTab* left, XTab* right);
			
			Area*				AddArea(XTab* left, YTab* top, XTab* right,
									YTab* bottom, BView* content, BSize minContentSize);
			Area*				AddArea(Row* row, Column* column,
									BView* content, BSize minContentSize);
			Area*				AddArea(XTab* left, YTab* top, XTab* right,
									YTab* bottom, BView* content);
			Area*				AddArea(Row* row, Column* column,
									BView* content);
			Area*				AreaOf(BView* control);

			XTab*				Left() const;
			XTab*				Right() const;
			YTab*				Top() const;
			YTab*				Bottom() const;
			
			void				RecoverLayout(BView* parent);
			
			LayoutStyleType		LayoutStyle() const;
			void				SetLayoutStyle(LayoutStyleType style);

	virtual	BSize				BaseMinSize();
	virtual	BSize				BaseMaxSize();
	virtual	BSize				BasePreferredSize();
	virtual	BAlignment			BaseAlignment();

	virtual	void				InvalidateLayout(bool children = false);

	virtual	bool				ItemAdded(BLayoutItem* item, int32 atIndex);
	virtual	void				ItemRemoved(BLayoutItem* item, int32 fromIndex);
	virtual	void				DerivedLayoutItems();
			
			char*				PerformancePath() const;
			void				SetPerformancePath(char* path);

			LinearSpec*			Solver();

private:
			Area*				_AreaForItem(BLayoutItem* item) const;
			void				_UpdateAreaConstraints();

			BSize				CalculateMinSize();
			BSize				CalculateMaxSize();
			BSize				CalculatePreferredSize();

private:
			LayoutStyleType		fLayoutStyle;
			bool				fActivated;

			LinearSpec			fSolver;

			XTab*				fLeft;
			XTab*				fRight;
			YTab*				fTop;
			YTab*				fBottom;
			BSize				fMinSize;
			BSize				fMaxSize;
			BSize				fPreferredSize;
			char*				fPerformancePath;
};

}	// namespace BALM

using BALM::BALMLayout;

#endif	// ALM_LAYOUT_H