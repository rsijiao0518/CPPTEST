
/*
 * File Property.cpp.
 *
 * This file is part of the source code of the software program
 * Vampire. It is protected by applicable
 * copyright laws.
 *
 * This source code is distributed under the licence found here
 * https://vprover.github.io/license.html
 * and in the source directory
 *
 * In summary, you are allowed to use Vampire for non-commercial
 * purposes but not allowed to distribute, modify, copy, create derivatives,
 * or use in competitions. 
 * For other uses of Vampire please contact developers for a different
 * licence, which we will make an effort to provide. 
 */
/**
 * @file Property.cpp (syntactic properties of problems)
 *
 * @since 06/06/2001 Manchester
 * @author Andrei Voronkov
 * @since 17/07/2003 Manchester, changed to new representation
 */

#include "Debug/Tracer.hpp"

#include "Lib/Int.hpp"
#include "Lib/Environment.hpp"

#include "Kernel/Clause.hpp"
#include "Kernel/FormulaUnit.hpp"
#include "Kernel/SortHelper.hpp"
#include "Kernel/SubformulaIterator.hpp"
#include "Kernel/Term.hpp"
#include "Kernel/Signature.hpp"
#include "Kernel/Inference.hpp"
#include "Kernel/TermIterators.hpp"

#include "Options.hpp"
#include "Statistics.hpp"
#include "FunctionDefinition.hpp"
#include "Property.hpp"
#include "SubexpressionIterator.hpp"

using namespace Lib;
using namespace Kernel;
using namespace Shell;

/**
 * Initialize Property. Must be applied to the preprocessed problem.
 *
 * @since 29/06/2002, Manchester
 */
Property::Property()
  : _goalClauses(0),
    _axiomClauses(0),
    _positiveEqualityAtoms(0),
    _equalityAtoms(0),
    _atoms(0),
    _goalFormulas(0),
    _axiomFormulas(0),
    _subformulas(0),
    _terms(0),
    _unitGoals(0),
    _unitAxioms(0),
    _hornGoals(0),
    _hornAxioms(0),
    _equationalClauses(0),
    _pureEquationalClauses(0),
    _groundUnitAxioms(0),
    _positiveAxioms(0),
    _groundPositiveAxioms(0),
    _groundGoals(0),
    _maxFunArity(0),
    _maxPredArity(0),
    _totalNumberOfVariables(0),
    _maxVariablesInClause(0),
    _props(0),
    _hasInterpreted(false),
    _hasInterpretedEquality(false),
    _hasNonDefaultSorts(false),
    _sortsUsed(0),
    _hasFOOL(false),
    _onlyFiniteDomainDatatypes(true),
    _knownInfiniteDomain(false),
    _allClausesGround(true),
    _allNonTheoryClausesGround(true),
    _allQuantifiersEssentiallyExistential(true),
    _smtlibLogic(SMTLIBLogic::SMT_UNDEFINED)
{
  _interpretationPresence.init(Theory::instance()->numberOfFixedInterpretations(), false);
  env.property = this;
  _symbolsInFormula = new DHSet<int>();
} // Property::Property

/**
 * Create a new property, scan the units with it and return the property.
 * @since 22/07/2011 Manchester
 */
Property* Property::scan(UnitList* units)
{
  CALL("Property::scan");

  // a bit of a hack, these counts belong in Property
  for(unsigned f=0;f<env.signature->functions();f++){ 
    env.signature->getFunction(f)->resetUsageCnt(); 
    env.signature->getFunction(f)->resetUnitUsageCnt(); 
   }
  for(unsigned p=0;p<env.signature->predicates();p++){ 
    env.signature->getPredicate(p)->resetUsageCnt(); 
    env.signature->getPredicate(p)->resetUnitUsageCnt(); 
   }

  Property* prop = new Property;
  prop->add(units);
  return prop;
} // Property::scan

/**
 * Destroy the property. If this property is used as env.property, set env.property to null.
 * @since 22/07/2011 Manchester
 */
Property::~Property()
{
  CALL("Property::~Property");
  if (this == env.property) {
    env.property = 0;
  }
}

/**
 * Add units and modify an existing property.
 * @since 29/06/2002 Manchester
 */
void Property::add(UnitList* units)
{
  CALL("Property::add(UnitList*)");

  UnitList::Iterator us(units);
  while (us.hasNext()) {
    scan(us.next());
  }

  if (_allClausesGround && _allQuantifiersEssentiallyExistential) {
    addProp(PR_ESSENTIALLY_GROUND);
  } else if (hasProp(PR_ESSENTIALLY_GROUND)) {
    dropProp(PR_ESSENTIALLY_GROUND);
  }

  // information about sorts is read from the environment, not from the problem
  if (env.sorts->hasSort()) {
    addProp(PR_SORTS);
  }
    
  // information about interpreted constant is read from the signature
  if (env.signature->strings()) {
    addProp(PR_HAS_STRINGS);
  }
  if (env.signature->integers()) {
    addProp(PR_HAS_INTEGERS);
  }
  if (env.signature->rationals()) {
    addProp(PR_HAS_RATS);
  }
  if (env.signature->reals()) {
    addProp(PR_HAS_REALS);
  }


  // determine the category after adding
  if (formulas() > 0) { // FOF, either FEQ or FNE
    if (_equalityAtoms == 0) {
      _category = FNE;
    }
    else {
      _category = FEQ;
    }
  }
  // no formulas in the input, one of NEQ, HEQ, PEQ, HNE, NNE, EPR, UEQ
  else if (_maxFunArity == 0) { // all function symbols are constants
    if (_pureEquationalClauses == clauses()) { // only equations, UEQ or PEQ
      if (clauses() == unitClauses()) { // all clauses are unit
	_category = UEQ;
      }
      else {
	_category = PEQ;
      }
    }
    else {
      _category = EPR;
    }
  }
  // one of NEQ, HEQ, PEQ, HNE, NNE, UEQ
  else if (_equationalClauses == 0 ) { // HNE, NNE
    if (clauses() == hornClauses()) { // all clauses are horn
      _category = HNE;
    }
    else {
      _category = NNE;
    }
  }
  // one of NEQ, HEQ, PEQ, UEQ
  else if (_pureEquationalClauses == clauses()) { // only equations, UEQ or PEQ
    if (clauses() == unitClauses()) { // all clauses are unit
      _category = UEQ;
    }
    else {
      _category = PEQ;
    }
  }
  // one of NEQ, HEQ
  else if (clauses() == hornClauses()) { // all clauses are horn
    _category = HEQ;
  }
  else {
    _category = NEQ;
  }
} // Property::add(const UnitList* units)

/**
 * Scan property from a unit.
 *
 * @since 29/06/2002 Manchester
 * @since 17/07/2003 Manchester, changed to non-pointer types,
 *        formula scanning added
 * @since 26/05/2007 Manchester, changed to use new datastructures
 */
void Property::scan(Unit* unit)
{
  CALL("Property::scan(const Unit*)");

  ASS(_symbolsInFormula);
  _symbolsInFormula->reset();

  if (unit->isClause()) {
    scan(static_cast<Clause*>(unit));
  }
  else {
    scan(static_cast<FormulaUnit*>(unit));
  }
  if (! hasProp(PR_HAS_FUNCTION_DEFINITIONS)) {
    FunctionDefinition::Def* def =
      FunctionDefinition::isFunctionDefinition(*unit);
    if (def) {
      addProp(PR_HAS_FUNCTION_DEFINITIONS);
      FunctionDefinition::deleteDef(def);
    }
  }

  DHSet<int>::Iterator it(*_symbolsInFormula);
  while(it.hasNext()){
    int symbol = it.next();
    if(symbol >= 0){
      env.signature->getFunction(symbol)->incUnitUsageCnt();
    }else{
      symbol = -symbol;
      env.signature->getPredicate(symbol)->incUnitUsageCnt();
    }
  }

} // Property::scan(const Unit* unit)

/**
 * Scan a clause.
 *
 * @param clause the clause
 * @since 29/06/2002 Manchester
 * @since 17/07/2003 Manchester, changed to non-pointer types
 * @since 27/08/2003 Vienna, changed to count variables
 * @since 26/05/2007 Manchester, changed to use new datastructures
 */
void Property::scan(Clause* clause)
{
  CALL("Property::scan(const Clause*)");

  int positiveLiterals = 0;
  int negativeLiterals = 0;
  int equationalLiterals = 0;
  int positiveEquationalLiterals = 0;
  int groundLiterals = 0;
  _variablesInThisClause = 0;

  for (int i = clause->length()-1;i >= 0;i--) {
    Literal* literal = (*clause)[i];
    if (literal->isPositive()) {
      positiveLiterals ++;
    }
    else {
      negativeLiterals ++;
    }

    if (literal->isEquality()) {
      equationalLiterals++;
      if (literal->isPositive()) {
	positiveEquationalLiterals++;
      }
    }

    bool goal = (clause->inputType()==Unit::CONJECTURE || clause->inputType()==Unit::NEGATED_CONJECTURE);
    bool unit = (clause->length() == 1);

    // 1 for context polarity, only used in formulas
    scan(literal,1,clause->length(),goal);

    SubtermIterator stit(literal);
    while (stit.hasNext()) {
      scan(stit.next(),unit,goal);
    }

    if (literal->shared() && literal->ground()) {
      groundLiterals++;
    }
  }
  int literals = positiveLiterals + negativeLiterals;
  _atoms += literals;

  if ( equationalLiterals > 0 ) {
    _equationalClauses ++;
    _equalityAtoms += equationalLiterals;
    _positiveEqualityAtoms += positiveEquationalLiterals;
  }
  if ( literals == equationalLiterals ) {
    _pureEquationalClauses ++;
  }

  if (clause->inputType() == Unit::AXIOM) {
    _axiomClauses ++;
    if ( literals == 1) {
      _unitAxioms ++;
      if ( groundLiterals == 1) {
	_groundUnitAxioms ++;
      }
    }
    if (positiveLiterals <= 1) {
      _hornAxioms ++;
    }
    if (negativeLiterals == 0) {
      _positiveAxioms ++;
      if (literals == groundLiterals) {
	_groundPositiveAxioms ++;
      }
    }
  }
  else {
    _goalClauses ++;
    if ( literals == 1) {
      _unitGoals ++;
    }
    if (positiveLiterals <= 1) {
      _hornGoals ++;
    }
    if (literals == groundLiterals) {
      _groundGoals ++;
    }
  }

  _totalNumberOfVariables += _variablesInThisClause;
  if (_variablesInThisClause > _maxVariablesInClause) {
    _maxVariablesInClause = _variablesInThisClause;
  }
  if (! hasProp(PR_HAS_X_EQUALS_Y) && hasXEqualsY(clause)) {
    addProp(PR_HAS_X_EQUALS_Y);
  }

  if (_variablesInThisClause > 0) {
    _allClausesGround = false;
    if(clause->inference()->rule()!=Inference::THEORY && clause->inference()->rule()!=Inference::FOOL_AXIOM){
      _allNonTheoryClausesGround = false;
    }
  }
} // Property::scan (const Clause* clause, bool isAxiom)


/**
 * Scan a formula unit.
 * @since 27/05/2007 flight Manchester-Frankfurt
 * @since 15/01/2014 Manchester, changed to use new hasXEqualsY
 * @author Andrei Voronkov
 */
void Property::scan(FormulaUnit* unit)
{
  CALL("Property::scan(const FormulaUnit*)");


  if (unit->inputType() == Unit::AXIOM) {
    _axiomFormulas ++;
  }
  else {
    _goalFormulas++;
  }
  Formula* f = unit->formula();

  SubexpressionIterator sei(f);
  while (sei.hasNext()) {
    SubexpressionIterator::Expression expr = sei.next();
    int polarity = expr.getPolarity();

    if (expr.isFormula()) {
      scan(expr.getFormula(), polarity);
    } else if (expr.isTerm()) {
      scan(expr.getTerm(),false,false); // only care about unit/goal when clausified
    } else {
      ASSERTION_VIOLATION;
    }
  }

  if (! hasProp(PR_HAS_X_EQUALS_Y)) {
    if (hasXEqualsY(f)) {
      addProp(PR_HAS_X_EQUALS_Y);
    }
  }
} // Property::scan


/**
 * Scan a formula.
 *
 * @since 17/07/2003 Manchester
 * @since 11/12/2004 Manchester, true and false added
 */
void Property::scan(Formula* f, int polarity)
{
  CALL("void Property::scan(Formula* formula, int polarity)");

  _subformulas++;
  switch(f->connective()) {
    case LITERAL: {
      _atoms++;
      Literal* lit = f->literal();
      if (lit->isEquality()) {
        _equalityAtoms++;
        if ((lit->isPositive() && polarity == 1) ||
            (!lit->isPositive() && polarity == -1) ||
            polarity == 0) {
          _positiveEqualityAtoms++;
        }
      }
      scan(lit,polarity,0,false); // 0 as not in clause, goal type irrelevant
      break;
    }
    case BOOL_TERM: {
      _hasFOOL = true;
      TermList ts = f->getBooleanTerm();
      if (ts.isVar()) {
        addProp(PR_HAS_BOOLEAN_VARIABLES);
      }
      break;
    }
    case FORALL:
      if (polarity != -1) {
        _allQuantifiersEssentiallyExistential = false;
      }
      break;
    case EXISTS:
      if (polarity != 1) {
        _allQuantifiersEssentiallyExistential = false;
      }
      break;
    default:
      break;
  }
} // Property::scan(const Formula&)

/**
 * If the sort is recognised by the properties, add information about it to the properties.
 * @since 04/05/2013 Manchester, array sorts removed
 * @author Andrei Voronkov
 */
void Property::scanSort(unsigned sort)
{
  CALL("Property::scanSort");

  if(!_usesSort.get(sort)){
    _sortsUsed++;
    _usesSort[sort]=true;
  }

  if (sort==Sorts::SRT_DEFAULT) {
    return;
  }
  _hasNonDefaultSorts = true;
  env.statistics->hasTypes=true;

  if(sort >= Sorts::FIRST_USER_SORT){
    if(env.sorts->isOfStructuredSort(sort,Sorts::StructuredSort::ARRAY)){
      addProp(PR_HAS_ARRAYS);
    }
    if (env.signature->isTermAlgebraSort(sort)) {
      TermAlgebra* ta = env.signature->getTermAlgebraOfSort(sort);
      if (!ta->finiteDomain()) {
        _onlyFiniteDomainDatatypes = false;
      }
      if (ta->infiniteDomain()) {
        _knownInfiniteDomain = true;
      }
      if (ta->allowsCyclicTerms()) {
        addProp(PR_HAS_CDT_CONSTRUCTORS); // co-algebraic data type
      } else {
        addProp(PR_HAS_DT_CONSTRUCTORS); // algebraic data type
      }
    }
    return;
  }

  switch(sort) {
  case Sorts::SRT_INTEGER:
    addProp(PR_HAS_INTEGERS);
    break;
  case Sorts::SRT_RATIONAL:
    addProp(PR_HAS_RATS);
    break;
  case Sorts::SRT_REAL:
    addProp(PR_HAS_REALS);
    break;
  case Sorts::SRT_BOOL:
    addProp(PR_HAS_BOOLEAN_VARIABLES);
    _hasFOOL = true;
    break;
  }
}

/**
 * Scan a literal.
 *
 * @param lit the literal
 * @since 29/06/2002 Manchester
 * @since 17/07/2003 Manchester, changed to non-pointer types
 * @since 27/05/2007 flight Manchester-Frankfurt, uses new datastructures
 */
void Property::scan(Literal* lit, int polarity, unsigned cLen, bool goal)
{
  CALL("Property::scan(const Literal*...)");

  if (lit->isEquality()) {
    scanSort(SortHelper::getEqualityArgumentSort(lit));
  }
  else {
    _symbolsInFormula->insert(-lit->functor());
    int arity = lit->arity();
    if (arity > _maxPredArity) {
      _maxPredArity = arity;
    }
    Signature::Symbol* pred = env.signature->getPredicate(lit->functor());
    static bool weighted = env.options->symbolPrecedence() == Options::SymbolPrecedence::WEIGHTED_FREQUENCY ||
                           env.options->symbolPrecedence() == Options::SymbolPrecedence::REVERSE_WEIGHTED_FREQUENCY;
    unsigned w = weighted ? cLen : 1; 
    for(unsigned i=0;i<w;i++){pred->incUsageCnt();}
    if(cLen==1){
      pred->markInUnit();
    }
    if(goal){
      pred->markInGoal();
    }

    OperatorType* type = pred->predType();
    for (int i=0; i<arity; i++) {
      scanSort(type->arg(i));
    }
  }

  scanForInterpreted(lit);

  if (!hasProp(PR_HAS_INEQUALITY_RESOLVABLE_WITH_DELETION) && lit->isEquality() && lit->shared()
     && ((lit->isNegative() && polarity == 1) || (!lit->isNegative() && polarity == -1) || polarity == 0)
     && !lit->ground() &&
     ( ( lit->nthArgument(0)->isVar() &&
	 !lit->nthArgument(1)->containsSubterm(*lit->nthArgument(0)) ) ||
       ( lit->nthArgument(1)->isVar() &&
	 !lit->nthArgument(0)->containsSubterm(*lit->nthArgument(1)) ))) {
    addProp(PR_HAS_INEQUALITY_RESOLVABLE_WITH_DELETION);
  }
} // Property::scan(Literal* lit)


/**
 * Scan a term arguments.
 *
 * @param ts the list of terms
 * @since 29/06/2002 Manchester
 * @since 17/07/2003 Manchester, changed to non-pointer types,
 *        also NUMERIC case added
 * @since 27/08/2003 Vienna, changed to count variables
 * @since 27/05/2007 flight Manchester-Frankfurt, changed to new datastructures
 */
void Property::scan(TermList ts,bool unit,bool goal)
{
  CALL("Property::scan(TermList)");
  _terms++;
  if (ts.isVar()) {
    _variablesInThisClause++;
    return;
  }

  ASS(ts.isTerm());
  Term* t = ts.term();

  if (t->isSpecial()) {
    _hasFOOL = true;
    switch(t->functor()) {
      case Term::SF_ITE:
        addProp(PR_HAS_ITE);
        break;

      case Term::SF_LET:
      case Term::SF_LET_TUPLE:
        addProp(PR_HAS_LET_IN);
        break;

      default:
        break;
    }
  } else {
    scanForInterpreted(t);

    _symbolsInFormula->insert(t->functor());
    Signature::Symbol* func = env.signature->getFunction(t->functor());
    func->incUsageCnt();
    if(unit){ func->markInUnit();}
    if(goal){ func->markInGoal();}

    int arity = t->arity();
    OperatorType* type = func->fnType();
    for (int i = 0; i < arity; i++) {
      scanSort(type->arg(i));
    }

    if (arity > _maxFunArity) {
      _maxFunArity = arity;
    }
  }
}

void Property::scanForInterpreted(Term* t)
{
  CALL("Property::scanInterpretation");

  Interpretation itp;
  if (t->isLiteral()) {
    Literal* lit = static_cast<Literal*>(t);
    if (!theory->isInterpretedPredicate(lit)) { return; }
    if (lit->isEquality()) {
      //cout << "this is interpreted equality " << t->toString() << endl;
      _hasInterpretedEquality=true;
      return; 
    }
    itp = theory->interpretPredicate(lit);
  }
  else {
    if (!theory->isInterpretedFunction(t)) { return; }
    itp = theory->interpretFunction(t);
  }
  _hasInterpreted = true;

  if(itp < _interpretationPresence.size()){
    _interpretationPresence[itp] = true;
  }

  if(Theory::isConversionOperation(itp)){
    addProp(PR_NUMBER_CONVERSION);
    return;
  }

  if (Theory::isPolymorphic(itp)) {
    OperatorType* type = t->isLiteral() ?
        env.signature->getPredicate(t->functor())->predType() : env.signature->getFunction(t->functor())->fnType();

    _polymorphicInterpretations.insert(std::make_pair(itp,type));
    return;
  }

  unsigned sort = Theory::getOperationSort(itp);
  if(Theory::isInequality(itp)){
    switch(sort){
      case Sorts::SRT_INTEGER : addProp(PR_INTEGER_COMPARISON);
        break;
      case Sorts::SRT_RATIONAL : addProp(PR_RAT_COMPARISON);
        break;
      case Sorts::SRT_REAL : addProp(PR_REAL_COMPARISON);
        break;
    }
  }
  else if(Theory::isLinearOperation(itp)){
    switch(sort){
      case Sorts::SRT_INTEGER : addProp(PR_INTEGER_LINEAR);
        break;
      case Sorts::SRT_RATIONAL : addProp(PR_RAT_LINEAR);
        break;
      case Sorts::SRT_REAL : addProp(PR_REAL_LINEAR);
        break;
    }
  }
  else if(Theory::isNonLinearOperation(itp)){
    switch(sort){
      case Sorts::SRT_INTEGER : addProp(PR_INTEGER_NONLINEAR);
        break;
      case Sorts::SRT_RATIONAL : addProp(PR_RAT_NONLINEAR);
        break;
      case Sorts::SRT_REAL : addProp(PR_REAL_NONLINEAR);
        break;
    }
  }
}

/**
 * Return the string representation of the CASC category.
 */
vstring Property::categoryString() const
{
  CALL("vstring Property::categoryString() const");
  return categoryToString(_category);
}
vstring Property::categoryToString(Category cat)
{
  switch (cat)
    {
    case NEQ:
      return "NEQ";
    case HEQ:
      return "HEQ";
    case PEQ:
      return "PEQ";
    case HNE:
      return "HNE";
    case NNE:
      return "NNE";
    case FEQ:
      return "FEQ";
    case FNE:
      return "FNE";
    case EPR:
      return "EPR";
    case UEQ:
      return "UEQ";
#if VDEBUG
    default:
      ASSERTION_VIOLATION;
      return "";
#endif
    }
} // categoryString


/**
 * Output the property to a string readable by a human. NOT ALL FIELDS
 * ARE CURRENTLY OUTPUT.
 * @since 27/08/2003 Vienna
 */
vstring Property::toString() const
{
  vstring result("TPTP class: ");
  result += categoryString() + "\n";

  if (clauses() > 0) {
    result += "Clauses: ";
    result += Int::toString(clauses());
    result += " (";
    result += Int::toString(_unitAxioms+_unitGoals);
    result += " unit, ";
    result += Int::toString(_goalClauses);
    result += " goal, ";
    result += Int::toString(_equationalClauses);
    result += " equational)\n";

    result += "Variables: ";
    result += Int::toString(_totalNumberOfVariables);
    result += " (";
    result += Int::toString(_maxVariablesInClause);
    result += " maximum in a single clause)\n";
  }

  if (formulas() > 0) {
    result += "Formulas: ";
    result += Int::toString(formulas());
    result += " (";
    result += Int::toString(_goalFormulas);
    result += " goal)\n";
    result += "Subformulas: ";
    result += Int::toString(_subformulas);
    result += "\n";
  }

  result += "Atoms: ";
  result += Int::toString(_atoms);
  result += " (";
  result += Int::toString(_equalityAtoms);
  result += " equality)\n";

  return result;
} // Property::toString


/**
 * True if the clause contains a positive literal X=Y.
 * @since 04/06/2004 Manchester
 * @since 27/05/2007 Frankfurt airport, changed to new datastructures
 * @since 15/01/2014 Manchester, reimplemented
 * @author Andrei Voronkov
 */
bool Property::hasXEqualsY(const Clause* c)
{
  CALL("Property::hasXEqualsY (const Clause*)");

  for (int i = c->length()-1; i >= 0; i--) {
    const Literal* lit = (*c)[i];
    if (lit->isNegative()) {
      continue;
    }
    if (!lit->isEquality()) {
      continue;
    }
    const TermList* ts1 = lit->args();
    if (!ts1->isVar()) {
      continue;
    }
    const TermList* ts2 = ts1->next();
    if (ts2->isVar() &&
	ts1->var() != ts2->var()) {
      return true;
    }
  }
  return  false;
} // Property::hasXEqualsY(const Clause*)

/**
 * True if the subformula formula would have a literal X=Y
 * after clausification.
 *
 *
 * @warning Works correctly only with rectified formulas (closed or open)
 * @param f the formula
 * @since 11/12/2004 Manchester, true and false added
 * @since 27/05/2007 flight Frankfurt-Lisbon, changed to new datastructures
 * @since 15/01/2014 Manchester, bug fix and improvement
 * @author Andrei Voronkov
 * @warning this function can be improved, but at a higher cost, it also does not treat let constructs
 *          and if-then-else terms
 */
bool Property::hasXEqualsY(const Formula* f)
{
  CALL("Property::hasXEqualsY (const Formula*)");

  MultiCounter posVars; // universally quantified variables in positive subformulas
  MultiCounter negVars; // universally quantified variables in negative subformulas

  Stack<const Formula*> forms;
  Stack<int> pols; // polarities
  forms.push(f);
  pols.push(1);
  while (!forms.isEmpty()) {
    f = forms.pop();
    int pol = pols.pop();

    switch (f->connective()) {
    case LITERAL:
      {
	const Literal* lit = f->literal();
	if (lit->isNegative()) {
	  break;
	}
	if (!lit->isEquality()) {
	  break;
	}
	const TermList* ts1 = lit->args();
	if (!ts1->isVar()) {
	  break;
	}
	const TermList* ts2 = ts1->next();
	if (!ts2->isVar()) {
	  break;
	}
	Var v1 = ts1->var();
	Var v2 = ts2->var();
	if (v1 == v2) {
	  break;
	}
	if (!lit->isPositive()) {
	  pol = -pol;
	}
	if (pol >= 0 && posVars.get(v1) && posVars.get(v2)) {
	  return true;
	}
	if (pol <= 0 && negVars.get(v1) && negVars.get(v2)) {
	  return true;
	}
      }
      break;

    case AND:
    case OR:
      {
	FormulaList::Iterator fs(f->args());
	while (fs.hasNext()) {
	  forms.push(fs.next());
	  pols.push(pol);
	}
      }
      break;

    case IMP:
      forms.push(f->left());
      pols.push(-pol);
      forms.push(f->right());
      pols.push(pol);
      break;

    case IFF:
    case XOR:
      forms.push(f->left());
      pols.push(0);
      forms.push(f->right());
      pols.push(0);
      break;

    case NOT:
      forms.push(f->uarg());
      pols.push(-pol);
      break;

    case FORALL:
      // remember universally quantified variables
      if (pol >= 0) {
	Formula::VarList::Iterator vs(f->vars());
	while (vs.hasNext()) {
	  posVars.inc(vs.next());
	}
      }
      forms.push(f->qarg());
      pols.push(pol);
      break;

  case EXISTS:
      // remember universally quantified variables
      if (pol <= 0) {
	Formula::VarList::Iterator vs(f->vars());
	while (vs.hasNext()) {
	  posVars.inc(vs.next());
	}
      }
      forms.push(f->qarg());
      pols.push(pol);
      break;

    case TRUE:
    case FALSE:
      break;

    case BOOL_TERM:
      return true;

#if VDEBUG
    default:
      ASSERTION_VIOLATION;
#endif
    }
  }
  return false;
} // Property::hasXEqualsY(const Formula* f)

/**
 * Transforms the property to an SQL command asserting this
 * property to the Spider database. An example of a command is
 * "UPDATE problem SET property=0,category='NNE' WHERE name='TOP019-1';".
 *
 * @since 04/05/2005 Manchester
 */
vstring Property::toSpider(const vstring& problemName) const
{
  return (vstring)"UPDATE problem SET property="
    + Int::toString((int)_props)
    + ",category='"
    + categoryString()
    + "' WHERE name='"
    + problemName +
    + "';";
} // Property::toSpider

