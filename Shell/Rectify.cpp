
/*
 * File Rectify.cpp.
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
 * @file Rectify.cpp
 * Implements (static) class Rectify for the rectification inference rule.
 * @since 21/12/2003 Manchester
 * @since 23/01/2004 Manchester, changed to use non-static objects
 */

#include "Lib/Environment.hpp"
#include "Lib/Metaiterators.hpp"
#include "Lib/Recycler.hpp"

#include "Kernel/Formula.hpp"
#include "Kernel/FormulaUnit.hpp"
#include "Kernel/Inference.hpp"
#include "Kernel/SortHelper.hpp"
#include "Kernel/Term.hpp"
#include "Kernel/TermIterators.hpp"
#include "Kernel/Unit.hpp"

#include "Indexing/TermSharing.hpp"

#include "VarManager.hpp"

#include "Rectify.hpp"

using namespace Shell;

bool Rectify::Renaming::tryGetBoundAndMarkUsed (int var,int& boundTo) const
{
  CALL("Rectify::Renaming::tryGetBoundAndMarkUsed");

  if ((unsigned)var >= _capacity) {
    return false;
  }
  VarUsageTrackingList* vs = _array[var];
  if (VarUsageTrackingList::isEmpty(vs)) {
    return false;
  }
  boundTo = vs->head().first;
  vs->headPtr()->second = true;
  return true;
} // Rectify::bound

Rectify::VarWithUsageInfo Rectify::Renaming::getBoundAndUsage(int var) const
{
  CALL("Rectify::Renaming::getBoundAndUsage");

  ASS_L((unsigned)var,_capacity);

  VarUsageTrackingList* vs = _array[var];
  ASS(VarUsageTrackingList::isNonEmpty(vs));
  return vs->head();
}

/**
 * Rectify the formula from this unit. If the input type of this unit
 * contains free variables, then ask Signature::sig to create an answer
 * atom.
 * 
 * @since 23/01/2004 Manchester, changed to use non-static objects
 * @since 06/06/2007 Manchester, changed to use new datastructures
 */
FormulaUnit* Rectify::rectify (FormulaUnit* unit0, bool removeUnusedVars)
{
  CALL("Rectify::rectify (Unit*...)");
  ASS(!unit0->isClause());

  FormulaUnit* unit = unit0;

  Formula* f = unit->formula();
  Rectify rect;
  rect._removeUnusedVars = removeUnusedVars;
  Formula* g = rect.rectify(f);

  VarList* vars = rect._free;

  if (f != g) {
    unit = new FormulaUnit(g,
			   new Inference1(Inference::RECTIFY,unit),
			   unit->inputType());
    if(unit0->included()) {
      unit->markIncluded();
    }
  }

  if (VarList::isNonEmpty(vars)) {
    //TODO do we know the sorts of vars?
    unit = new FormulaUnit(new QuantifiedFormula(FORALL,vars,0,g),
			   new Inference1(Inference::CLOSURE,unit),
			   unit->inputType());
    if(unit0->included()) {
      unit->markIncluded();
    }
  }
  return unit;
} // Rectify::rectify (Unit& unit)

/**
 * Rectify formulas in @c units
 */
void Rectify::rectify(UnitList*& units)
{
  CALL("Rectify::rectify(UnitList*&)");

  UnitList::DelIterator us(units);
  while (us.hasNext()) {
    Unit* u = us.next();
    if(u->isClause()) {
      continue;
    }
    FormulaUnit* fu = static_cast<FormulaUnit*>(u);
    FormulaUnit* v = rectify(fu);
    if (v != fu) {
	us.replace(v);
    }
  }
}

/**
 * Destroy all renaming lists.
 * @since 07/06/2007 Manchester
 */
Rectify::Renaming::~Renaming ()
{
  CALL("Rectify::Renaming::~Renaming");

  for (int i = _capacity-1;i >= 0;i--) {
    VarUsageTrackingList::destroy(_array[i]);
    _array[i] = 0;
  }

  if(_used) {
    Recycler::release(_used);
  }
} // Renaming::~Renaming



/**
 * Rectify a special term
 */
Term* Rectify::rectifySpecialTerm(Term* t)
{
  CALL("Rectify::rectifySpecialTerm");

  Term::SpecialTermData* sd = t->getSpecialData();
  switch(t->functor()) {
  case Term::SF_ITE:
  {
    ASS_EQ(t->arity(),2);
    Formula* c = rectify(sd->getCondition());
    TermList th = rectify(*t->nthArgument(0));
    TermList el = rectify(*t->nthArgument(1));
    if(c==sd->getCondition() && th==*t->nthArgument(0) && el==*t->nthArgument(1)) {
	return t;
    }
    return Term::createITE(c, th, el, sd->getSort());
  }
  case Term::SF_LET:
  {
    ASS_EQ(t->arity(),1);

    bindVars(sd->getVariables());
    TermList binding = rectify(sd->getBinding());
    /**
     * We don't need to remove unused variables from the body of a functions,
     * otherwise the rectified list of variables might not fix the arity of the
     * let functor. So, temporarily disable _removeUnusedVars;
     */
    bool removeUnusedVars = _removeUnusedVars;
    _removeUnusedVars = false;
    VarList* variables = rectifyBoundVars(sd->getVariables());
    _removeUnusedVars = removeUnusedVars; // restore the status quo
    unbindVars(sd->getVariables());

    ASS_EQ(VarList::length(variables),
           VarList::length(sd->getVariables()));

    TermList contents = rectify(*t->nthArgument(0));
    if (sd->getVariables() == variables && binding == sd->getBinding() && contents == *t->nthArgument(0)) {
      return t;
    }
    return Term::createLet(sd->getFunctor(), variables, binding, contents, sd->getSort());
  }
  case Term::SF_LET_TUPLE:
  {
    ASS_EQ(t->arity(),1);

    TermList binding = rectify(sd->getBinding());
    TermList contents = rectify(*t->nthArgument(0));

    if (binding == sd->getBinding() && contents == *t->nthArgument(0)) {
      return t;
    }
    return Term::createTupleLet(sd->getFunctor(), sd->getTupleSymbols(), binding, contents, sd->getSort());
  }
  case Term::SF_FORMULA:
  {
    ASS_EQ(t->arity(),0);
    Formula* orig = rectify(sd->getFormula());
    if(orig==sd->getFormula()) {
      return t;
    }
    return Term::createFormula(orig);
  }
  case Term::SF_TUPLE:
  {
    ASS_EQ(t->arity(),0);
    Term* rectifiedTupleTerm = rectify(sd->getTupleTerm());
    if (rectifiedTupleTerm == sd->getTupleTerm()) {
      return t;
    }
    return Term::createTuple(rectifiedTupleTerm);
  }
  default:
    ASSERTION_VIOLATION;
  }
  ASSERTION_VIOLATION;
}

/**
 * Rectify a compound term.
 * @since 01/11/2005 Bellevue
 * @since 04/06/2007 Frankfurt Airport, changed to new datastructures
 */
Term* Rectify::rectify (Term* t)
{
  CALL("Rectify::rectify(Term*)");

  if (t->shared() && t->ground()) {
    return t;
  }

  if(t->isSpecial()) {
    return rectifySpecialTerm(t);
  }

  Term* s = new(t->arity()) Term(*t);
  if (rectify(t->args(),s->args())) {
    if(TermList::allShared(s->args())) {
      return env.sharing->insert(s);
    }
    else {
      return s;
    }
  }
  // term not changed
  s->destroy();
  return t;
} // Rectify::rectify (Term*)

Literal* Rectify::rectifyShared(Literal* lit)
{
  CALL("Rectify::rectifyShared");
  ASS(lit->shared());

  return SubstHelper::apply(lit, *this);
}

/**
 * Rectify a literal.
 * @since 24/03/2008 Torrevieja
 */
Literal* Rectify::rectify (Literal* l)
{
  CALL("Rectify::rectify(Literal*)");

  if (l->shared()) {
    if(l->ground()) {
      return l;
    }
//    //this is faster than the way below
//    return rectifyShared(l);
  }

  Literal* m = new(l->arity()) Literal(*l);
  if (rectify(l->args(),m->args())) {
    if(TermList::allShared(m->args())) {
      if(l->isEquality() && m->nthArgument(0)->isVar() && m->nthArgument(1)->isVar()) {
	ASS(l->shared());
	unsigned srt = SortHelper::getEqualityArgumentSort(l);
	return env.sharing->insertVariableEquality(m, srt);
      }
      else {
	return env.sharing->insert(m);
      }
    }
    else {
      return m;
    }
  }
  // literal not changed
  m->destroy();
  return l;
} // Rectify::rectify (Literal*)

/**
 * Rectify a list of terms @b from to the list of terms @b to.
 * Return true if the list has changed.
 * @since 24/03/2008 Torrevieja
 */
bool Rectify::rectify(TermList* from,TermList* to)
{
  CALL("Rectify::rectify(TermList* ...)");

  bool changed = false;
  while (! from->isEmpty()) {
    if (from->isVar()) {
      int v = from->var();
      int newV = rectifyVar(v);
      to->makeVar(newV);
      if (v != newV) { // rename variable to itself
	changed = true;
      }
    }
    else { // from is not a variable
      Term* f = from->term();
      Term* t = rectify(f);
      to->setTerm(t);
      if (f != t) {
	changed = true;
      }
    }
    from = from->next();
    ASS(! to->isEmpty());
    to = to->next();
  }
  ASS(to->isEmpty());
  return changed;
} // Rectify::rectify (TermList*,...)

/**
 * Rectify variable @b v and return the result
 */
unsigned Rectify::rectifyVar(unsigned v)
{
  CALL("Rectify::rectifyVar");

  int newV;
  if (! _renaming.tryGetBoundAndMarkUsed(v,newV)) {
    newV = _renaming.bind(v);
    _free = new VarList(newV,_free);
  }
  return newV;
}


/**
 * Rectify term @b t
 */
TermList Rectify::rectify(TermList t)
{
  CALL("Rectify::rectify");

  if(t.isTerm()) {
    return TermList(rectify(t.term()));
  }
  ASS(t.isOrdinaryVar());
  return TermList(rectifyVar(t.var()), false);
}

/**
 * Rectify a formula.
 *
 * @param f the formula
 *
 * @since 27/06/2002 Manchester
 * @since 30/08/2002 Torrevieja, changed
 * @since 16/01/2004 Manchester, changed to use bound variables
 * @since 23/01/2004 Manchester, changed to use non-static objects
 * @since 11/12/2004 Manchester, true and false added
 * @since 04/07/2007 Frankfurt Airport, changed to new data structures
 */
Formula* Rectify::rectify (Formula* f)
{
  CALL("Rectify::rectify (Formula*)");

  switch (f->connective()) {
  case LITERAL: 
  {
    Literal* lit = rectify(f->literal());
    return lit == f->literal() ? f : new AtomicFormula(lit);
  }

  case AND: 
  case OR: 
  {
    FormulaList* newArgs = rectify(f->args());
    if (newArgs == f->args()) {
      return f;
    }
    return new JunctionFormula(f->connective(), newArgs);
  }

  case IMP: 
  case IFF: 
  case XOR:
  {
    Formula* l = rectify(f->left());
    Formula* r = rectify(f->right());
    if (l == f->left() && r == f->right()) {
      return f;
    }
    return new BinaryFormula(f->connective(), l, r);
  }

  case NOT:
  {
    Formula* arg = rectify(f->uarg());
    if (f->uarg() == arg) {
      return f;
    }
    return new NegatedFormula(arg);
  }

  case FORALL: 
  case EXISTS:
  {
    bindVars(f->vars());
    Formula* arg = rectify(f->qarg());
    VarList* vs = rectifyBoundVars(f->vars());
    unbindVars(f->vars());
    if (vs == f->vars() && arg == f->qarg()) {
      return f;
    }
    if(VarList::isEmpty(vs)) {
      return arg;
    }
    //TODO should update the sorts from f->sorts() wrt to updated vs
    //     or is the rectification just renaming, if so f->sorts can 
    //     just be reused
    return new QuantifiedFormula(f->connective(),vs,0,arg);
  }

  case TRUE:
  case FALSE:
    return f;

  case BOOL_TERM:
     return new BoolTermFormula(rectify(f->getBooleanTerm()));

#if VDEBUG
  default:
    ASSERTION_VIOLATION;
#endif
  }
} // Rectify::rectify (Formula*)

/**
 * Undo the last binding for variable var.
 * @since 07/06/2007 Manchester
 */
void Rectify::Renaming::undoBinding (int var)
{
  CALL("Rectify::Renaming::undoBinding");

  ASS(var < (int)_capacity);
  ASS(var >= 0);

  VarUsageTrackingList::pop(_array[var]);
} // Rectify::Renaming::undoBinding

/**
 * Bind var to a new variable and return the new variable.
 * @since 07/06/2007 Manchester
 */
int Rectify::Renaming::bind (int var)
{
  CALL("Rectify::Renaming::bind");

  int result;

  if(VarManager::varNamePreserving()) {
    if(!_used) {
      Recycler::get(_used);
      _used->reset();
    }
    if(_used->insert(var)) {
      result=var;
    }
    else {
      result=VarManager::getVarAlias(var);
    }
  }
  else {
    result = _nextVar++;
  }
  VarUsageTrackingList::push(make_pair(result,false), get(var));

  return result;
} // Rectify::Renaming::bind


/**
 * Add fresh bindings to a list of variables
 */
void Rectify::bindVars(VarList* vs)
{
  CALL ("Rectify::bindVars (VarList*)");

  VarList::Iterator vit(vs);
  while(vit.hasNext()) {
    int v = vit.next();
    _renaming.bind(v);
  }
}

/**
 * Undo bindings to variables of a list
 */
void Rectify::unbindVars(VarList* vs)
{
  CALL ("Rectify::unbindVars (VarList*)");

  VarList::Iterator vit(vs);
  while(vit.hasNext()) {
    int v = vit.next();
    _renaming.undoBinding(v);
  }
}

/**
 * Rectify a list of variables.
 *
 * @param vs the list to rectify
 */
Rectify::VarList* Rectify::rectifyBoundVars (VarList* vs)
{
  CALL ("Rectify::rectifyBoundVars(VarList*)");

  if (VarList::isEmpty(vs)) {
    return vs;
  }

  Stack<VarList*> args;
  while (VarList::isNonEmpty(vs)) {
    args.push(vs);
    vs = vs->tail();
  }

  VarList* res = args.top()->tail();
  ASS(VarList::isEmpty(res));

  while (args.isNonEmpty()) {
    vs = args.pop();

    VarList* vtail = vs->tail();
    VarList* ws = res; // = rectifyBoundVars(vtail);

    int v = vs->head();
    int w;
    VarWithUsageInfo wWithUsg = _renaming.getBoundAndUsage(v);
    if (wWithUsg.second || !_removeUnusedVars) {
      w = wWithUsg.first;

      if (v == w && vtail == ws) {
        res = vs;
      } else {
        res = new VarList(w,ws);
      }
    }
    // else nothing, because "else" means dropping the variable from the list and returning ws, but res == ws already ...
  }

  return res;
} // Rectify::rectify(const VarList& ...)


/**
 * Rectify a list of formulas.
 * @since 08/06/2007 Manchester
 */
FormulaList* Rectify::rectify (FormulaList* fs)
{
  CALL ("Rectify::rectify (FormulaList*)");

  Stack<FormulaList*>* els;
  Recycler::get(els);
  els->reset();

  FormulaList* el = fs;
  while(el) {
    els->push(el);
    el = el->tail();
  }

  FormulaList* res = 0;

  bool modified = false;
  while(els->isNonEmpty()) {
    FormulaList* el = els->pop();
    Formula* f = el->head();
    Formula* g = rectify(f);
    if(!modified && f!=g) {
      modified = true;
    }
    if(modified) {
      FormulaList::push(g, res);
    }
    else {
      res = el;
    }
  }

  Recycler::release(els);
  return res;

//  if (fs->isEmpty()) {
//    return fs;
//  }
//  Formula* f = fs->head();
//  FormulaList* tail = fs->tail();
//  Formula* g = rectify(f);
//  FormulaList* gs = rectify(tail);
//
//  if (f == g && tail == gs) {
//    return fs;
//  }
//  return new FormulaList(g,gs);
} // Rectify::rectify(FormulaList*)

/**
 * Fill all values by zeros.
 * @since 13/01/2008 Manchester
 */
void Rectify::Renaming::fillInterval (size_t start,size_t end)
{
  for (unsigned i = start;i < end;i++) {
    _array[i] = 0;
  }
} // fillInterval
