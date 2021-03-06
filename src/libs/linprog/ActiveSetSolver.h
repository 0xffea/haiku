/*
 * Copyright 2010, Clemens Zeidler <haiku@clemens-zeidler.de>
 * Distributed under the terms of the MIT License.
 */
#ifndef ACTICE_SET_SOLVER_H
#define ACTICE_SET_SOLVER_H


#include "QPSolverInterface.h"


class EquationSystem {
public:
								EquationSystem(int32 rows, int32 columns);
								~EquationSystem();

			void				SetRows(int32 rows);
			int32				Rows();
			int32				Columns();

	inline	double&				A(int32 row, int32 column);
	inline	double&				B(int32 row);
			/*! Copy the solved variables into results, keeping the original
			variable order. */
	inline	void				Results(double* results, int32 size);

	inline	void				SwapColumn(int32 i, int32 j);
	inline	void				SwapRow(int32 i, int32 j);

			bool				GaussJordan();
			/*! Gauss Jordan elimination just for one column, the diagonal
			element must be none zero. */
			void				GaussJordan(int32 column);

			void				RemoveLinearlyDependentRows();
			void				RemoveUnusedVariables();

			void				MoveColumnRight(int32 i, int32 target);

			void				Print();
private:
			int32*				fRowIndices;
			int32*				fColumnIndices;
			double**			fMatrix;
			double*				fB;
			int32				fRows;
			int32				fColumns;
};


class ActiveSetSolver : public QPSolverInterface {
public:
								ActiveSetSolver(LinearSpec* linearSpec);
								~ActiveSetSolver();

			ResultType			Solve();

			bool				VariableAdded(Variable* variable);
			bool				VariableRemoved(Variable* variable);
			bool				VariableRangeChanged(Variable* variable);

			bool				ConstraintAdded(Constraint* constraint);
			bool				ConstraintRemoved(Constraint* constraint);
			bool				LeftSideChanged(Constraint* constraint);
			bool				RightSideChanged(Constraint* constraint);
			bool				OperatorChanged(Constraint* constraint);

			bool				SaveModel(const char* fileName);

			BSize				MinSize(Variable* width, Variable* height);
			BSize				MaxSize(Variable* width, Variable* height);

public:
			void				_RemoveSoftConstraint(ConstraintList& list);
			void				_AddSoftConstraint(const ConstraintList& list);

	const	VariableList&		fVariables;
	const	ConstraintList&		fConstraints;

			ConstraintList		fVariableGEConstraints;
			ConstraintList		fVariableLEConstraints;
};


#endif // ACTICE_SET_SOLVER_H
