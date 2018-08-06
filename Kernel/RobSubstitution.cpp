
/*
 * File RobSubstitution.cpp.
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
 * @file RobSubstitution.cpp
 * Implements polynomial modification of the Robinson unification algorithm.
 */

#include <iostream>

#include "Lib/Environment.hpp"

#include "Lib/Hash.hpp"
#include "Lib/DArray.hpp"
#include "Lib/List.hpp"
#include "Lib/Random.hpp"
#include "Lib/DHSet.hpp"
#include "Lib/DHMap.hpp"
#include "Lib/SkipList.hpp"
#include "Lib/Int.hpp"

#include "Clause.hpp"
#include "Renaming.hpp"
#include "SortHelper.hpp"
#include "Term.hpp"
#include "TermIterators.hpp"

#include "Indexing/TermSharing.hpp"

#include "RobSubstitution.hpp"

#if VDEBUG

#include "Debug/Tracer.hpp"

using namespace Debug;
#endif

#define DEBUG_RESULT_WEIGHT_COMPUTATION 0

namespace Kernel
{

using namespace std;
using namespace Lib;

const int RobSubstitution::AUX_INDEX=-3;
const int RobSubstitution::SPECIAL_INDEX=-2;
const int RobSubstitution::UNBOUND_INDEX=-1;

/**
 * Unify @b t1 and @b t2, and return true iff it was successful.
 */
bool RobSubstitution::unify(TermList t1,int index1, TermList t2, int index2)
{
  CALL("RobSubstitution::unify/4");
  return unify(TermSpec(t1,index1), TermSpec(t2,index2));
}

/**
 * Unify arguments of @b t1 and @b t2, and return true iff it was successful.
 *
 * @b t1 and @b t2 can be either terms or literals.
 */
bool RobSubstitution::unifyArgs(Term* t1,int index1, Term* t2, int index2)
{
  CALL("RobSubstitution::unifyArgs");
  ASS_EQ(t1->functor(),t2->functor());

  TermList t1TL(t1);
  TermList t2TL(t2);
  return unify(TermSpec(t1TL,index1), TermSpec(t2TL,index2));
}

bool RobSubstitution::match(TermList base,int baseIndex,
	TermList instance, int instanceIndex)
{
  CALL("RobSubstitution::match(TermList...)");
  return match(TermSpec(base,baseIndex), TermSpec(instance,instanceIndex));
}
/**
 * Match arguments of @b t1 and @b t2, and return true iff it was successful.
 *
 * @b t1 and @b t2 can be either terms or literals.
 */
bool RobSubstitution::matchArgs(Term* base,int baseIndex,
	Term* instance, int instanceIndex)
{
  CALL("RobSubstitution::match(Literal*...)");
  ASS_EQ(base->functor(),instance->functor());

  TermList baseTL(base);
  TermList instanceTL(instance);
  return match(TermSpec(baseTL,baseIndex), TermSpec(instanceTL,instanceIndex));
}

/**
 * Bind variables from @b denormalizedIndex to variables in @b normalIndex
 * in a way, that applying the substitution to a term in @b denormalizedIndex
 * would give the same result as first renaming variables and then applying
 * the substitution in @b normalIndex.
 *
 * @warning All variables, that occured in some term that was matched or unified
 * in @b normalIndex, must be also present in the @b normalizer.
 */
void RobSubstitution::denormalize(const Renaming& normalizer, int normalIndex, int denormalizedIndex)
{
  CALL("RobSubstitution::denormalize");

  VirtualIterator<Renaming::Item> nit=normalizer.items();
  while(nit.hasNext()) {
    Renaming::Item itm=nit.next();
    VarSpec normal(itm.second, normalIndex);
    VarSpec denormalized(itm.first, denormalizedIndex);
    ASS(!_bank.find(denormalized));
    bindVar(denormalized,normal);
  }
}

bool RobSubstitution::isUnbound(VarSpec v) const
{
  CALL("RobSubstitution::isUnbound");
  for(;;) {
    TermSpec binding;
    bool found=_bank.find(v,binding);
    if(!found || binding.index==UNBOUND_INDEX) {
      return true;
    } else if(binding.term.isTerm()) {
      return false;
    }
    v=getVarSpec(binding);
  }
}

/**
 * If special variable @b specialVar is bound to a proper term,
 * return a term, thjat has the same top functor. Otherwise
 * return an arbitrary variable.
 */
TermList RobSubstitution::getSpecialVarTop(unsigned specialVar) const
{
  VarSpec v(specialVar, SPECIAL_INDEX);
  for(;;) {
    TermSpec binding;
    bool found=_bank.find(v,binding);
    if(!found || binding.index==UNBOUND_INDEX) {
      static TermList auxVarTerm(1,false);
      return auxVarTerm;
    } else if(binding.term.isTerm()) {
      return binding.term;
    }
    v=getVarSpec(binding);
  }
}

/**
 * If @b t is a non-variable, return @b t. Else, if @b t is a variable bound to
 * a non-variable term, return the term. Otherwise, return the root variable
 * to which @b t belongs.
 */
RobSubstitution::TermSpec RobSubstitution::derefBound(TermSpec t) const
{
  CALL("RobSubstitution::derefBound");
  if(t.term.isTerm()) {
    return t;
  }
  VarSpec v=getVarSpec(t);
  for(;;) {
    TermSpec binding;
    bool found=_bank.find(v,binding);
    if(!found || binding.index==UNBOUND_INDEX) {
      return TermSpec(v);
    } else if(binding.term.isTerm()) {
      return binding;
    }
    v=getVarSpec(binding);
  }
}

RobSubstitution::TermSpec RobSubstitution::deref(VarSpec v) const
{
  CALL("RobSubstitution::deref");
  for(;;) {
    TermSpec binding;
    bool found=_bank.find(v,binding);
    if(!found) {
      binding.index=UNBOUND_INDEX;
      binding.term.makeVar(_nextUnboundAvailable++);
      const_cast<RobSubstitution&>(*this).bind(v,binding);
      return binding;
    } else if(binding.index==UNBOUND_INDEX || binding.term.isTerm()) {
      return binding;
    }
    v=getVarSpec(binding);
  }
}

void RobSubstitution::bind(const VarSpec& v, const TermSpec& b)
{
  CALL("RobSubstitution::bind");
  ASSERT_VALID(b.term);
  //Aux terms don't contain special variables, ergo
  //should be shared.
  ASS(!b.term.isTerm() || b.index!=AUX_INDEX || b.term.term()->shared());
  ASS_NEQ(v.index, UNBOUND_INDEX);

  if(bdIsRecording()) {
    bdAdd(new BindingBacktrackObject(this, v));
  }
  _bank.set(v,b);
}

void RobSubstitution::bindVar(const VarSpec& var, const VarSpec& to)
{
  CALL("RobSubstitution::bindVar");
  ASS_NEQ(var,to);

  bind(var,TermSpec(to));
}

RobSubstitution::VarSpec RobSubstitution::root(VarSpec v) const
{
  CALL("RobSubstitution::root");
  for(;;) {
    TermSpec binding;
    bool found=_bank.find(v,binding);
    if(!found || binding.index==UNBOUND_INDEX || binding.term.isTerm()) {
      return v;
    }
    v=getVarSpec(binding);
  }
}

void RobSubstitution::makeEqual(VarSpec v1, VarSpec v2, TermSpec target)
{
  CALL("RobSubstitution::makeEqual");

  v1=root(v1);
  v2=root(v2);
  if(v1==v2) {
    bind(v2,target);
    return;
  }
  if(Random::getBit()) {
    bindVar(v1,v2);
    bind(v2,target);
  } else {
    bindVar(v2,v1);
    bind(v1,target);
  }
}

void RobSubstitution::unifyUnbound(VarSpec v, TermSpec ts)
{
  CALL("RobSubstitution::unifyUnbound");

  v=root(v);

  ASS(isUnbound(v));

  if(ts.isVar()) {
    VarSpec v2=root(getVarSpec(ts));
    if(v!=v2) {
    	makeEqual(v, v2, deref(v2));
    }
  } else {
    bind(v,ts);
  }
}


void RobSubstitution::swap(TermSpec& ts1, TermSpec& ts2)
{
  TermSpec aux=ts1;
  ts1=ts2;
  ts2=aux;
}


bool RobSubstitution::occurs(VarSpec vs, TermSpec ts)
{
  vs=root(vs);
  Stack<TermSpec> toDo(8);
  if(ts.isVar()) {
    ts=derefBound(ts);
    if(ts.isVar()) {
      return false;
    }
  }
  typedef DHSet<VarSpec, VarSpec::Hash1> EncounterStore;
  static EncounterStore encountered;
  encountered.reset();

  for(;;){
    ASS(ts.term.isTerm());
    VariableIterator vit(ts.term.term());
    while(vit.hasNext()) {
      VarSpec tvar=root(getVarSpec(vit.next(), ts.index));
      if(tvar==vs) {
	return true;
      }
      if(!encountered.find(tvar)) {
	TermSpec dtvar=derefBound(TermSpec(tvar));
	if(!dtvar.isVar()) {
	  encountered.insert(tvar);
	  toDo.push(dtvar);
	}
      }
    }

    if(toDo.isEmpty()) {
      return false;
    }
    ts=toDo.pop();
  }
}

bool RobSubstitution::unify(TermSpec t1, TermSpec t2)
{
  CALL("RobSubstitution::unify/2");

  if(t1.sameTermContent(t2)) {
    return true;
  }

  bool mismatch=false;
  BacktrackData localBD;
  bdRecord(localBD);

  static Stack<TTPair> toDo(64);
  static Stack<TermList*> subterms(64);
  ASS(toDo.isEmpty() && subterms.isEmpty());

  typedef DHSet<TTPair,TTPairHash> EncStore;
  EncStore encountered;
  encountered.reset();

  for(;;) {
    TermSpec dt1=derefBound(t1);
    TermSpec dt2=derefBound(t2);

    if(dt1.sameTermContent(dt2)) {
    } else if(dt1.isVar()) {
      VarSpec v1=getVarSpec(dt1);
      if(occurs(v1, dt2)) {
	mismatch=true;
	break;
      }
      bind(v1,dt2);
    } else if(dt2.isVar()) {
      VarSpec v2=getVarSpec(dt2);
      if(occurs(v2, dt1)) {
	mismatch=true;
	break;
      }
      bind(v2,dt1);
    } else {
      TermList* ss=&dt1.term;
      TermList* tt=&dt2.term;

      for (;;) {
        TermSpec tsss(*ss,dt1.index);
        TermSpec tstt(*tt,dt2.index);

        if (!tsss.sameTermContent(tstt) && TermList::sameTopFunctor(*ss,*tt)) {
          ASS(ss->isTerm() && tt->isTerm());

          Term* s = ss->term();
          Term* t = tt->term();
          ASS(s->arity() > 0);
          ASS(s->functor() == t->functor());

          ss = s->args();
          tt = t->args();
          if (! ss->next()->isEmpty()) {
            subterms.push(ss->next());
            subterms.push(tt->next());
          }
        } else {
          if (! TermList::sameTopFunctor(*ss,*tt)) {
            if(ss->isVar()||tt->isVar()) {
              TTPair itm(tsss,tstt);
              if((itm.first.isVar() && isUnbound(getVarSpec(itm.first))) ||
        	  (itm.second.isVar() && isUnbound(getVarSpec(itm.second))) ) {
                toDo.push(itm);
              } else if(!encountered.find(itm)) {
        	  toDo.push(itm);
        	  encountered.insert(itm);
        	}
            } else {
              mismatch=true;
              break;
            }
          }

          if (subterms.isEmpty()) {
            break;
          }
          tt = subterms.pop();
          ss = subterms.pop();
          if (! ss->next()->isEmpty()) {
            subterms.push(ss->next());
            subterms.push(tt->next());
          }
        }
      }
    }

    if(toDo.isEmpty()) {
      break;
    }
    t1=toDo.top().first;
    t2=toDo.pop().second;
  }

  if(mismatch) {
    subterms.reset();
    toDo.reset();
  }

  bdDone();

  if(mismatch) {
    localBD.backtrack();
  } else {
    if(bdIsRecording()) {
      bdCommit(localBD);
    }
    localBD.drop();
  }

  return !mismatch;
}

/**
 * Matches @b instance term onto the @b base term.
 * Ordinary variables behave, as one would expect
 * during matching, but special variables aren't
 * being assigned only in the @b base term, but in
 * the instance ass well. (Special variables appear
 * only in internal terms of substitution trees and
 * this behavior allows easy instance retrieval.)
 */
bool RobSubstitution::match(TermSpec base, TermSpec instance)
{
  CALL("RobSubstitution::match(TermSpec...)");

  if(base.sameTermContent(instance)) {
    return true;
  }

  bool mismatch=false;
  BacktrackData localBD;
  bdRecord(localBD);

  static Stack<TermList*> subterms(64);
  ASS(subterms.isEmpty());

  TermList* bt=&base.term;
  TermList* it=&instance.term;

  TermSpec binding1;
  TermSpec binding2;

  for (;;) {
    TermSpec bts(*bt,base.index);
    TermSpec its(*it,instance.index);

    if (!bts.sameTermContent(its) && TermList::sameTopFunctor(bts.term,its.term)) {
      Term* s = bts.term.term();
      Term* t = its.term.term();
      ASS(s->arity() > 0);

      bt = s->args();
      it = t->args();
    } else {
      if (! TermList::sameTopFunctor(bts.term,its.term)) {
	if(bts.term.isSpecialVar()) {
	  VarSpec bvs(bts.term.var(), SPECIAL_INDEX);
	  if(_bank.find(bvs, binding1)) {
	    ASS_EQ(binding1.index, base.index);
	    bt=&binding1.term;
	    continue;
	  } else {
	    bind(bvs,its);
	  }
	} else if(its.term.isSpecialVar()) {
	  VarSpec ivs(its.term.var(), SPECIAL_INDEX);
	  if(_bank.find(ivs, binding2)) {
	    ASS_EQ(binding2.index, instance.index);
	    it=&binding2.term;
	    continue;
	  } else {
	    bind(ivs,bts);
	  }
	} else if(bts.term.isOrdinaryVar()) {
	  VarSpec bvs(bts.term.var(), bts.index);
	  if(_bank.find(bvs, binding1)) {
	    ASS_EQ(binding1.index, instance.index);
	    if(!TermList::equals(binding1.term, its.term))
	    {
	      mismatch=true;
	      break;
	    }
	  } else {
	    bind(bvs,its);
	  }
	} else {
	  mismatch=true;
	  break;
	}
      }

      if (subterms.isEmpty()) {
	break;
      }
      bt = subterms.pop();
      it = subterms.pop();
    }
    if (!bt->next()->isEmpty()) {
      subterms.push(it->next());
      subterms.push(bt->next());
    }
  }

  bdDone();

  subterms.reset();


  if(mismatch) {
    localBD.backtrack();
  } else {
    if(bdIsRecording()) {
      bdCommit(localBD);
    }
    localBD.drop();
  }

  return !mismatch;
}


Literal* RobSubstitution::apply(Literal* lit, int index) const
{
  CALL("RobSubstitution::apply(Literal*...)");
  static DArray<TermList> ts(32);

  if (lit->ground()) {
    return lit;
  }

  int arity = lit->arity();
  ts.ensure(arity);
  int i = 0;
  for (TermList* args = lit->args(); ! args->isEmpty(); args = args->next()) {
    ts[i++]=apply(*args,index);
  }
  return Literal::create(lit,ts.array());
}

TermList RobSubstitution::apply(TermList trm, int index) const
{
  CALL("RobSubstitution::apply(TermList...)");

  static Stack<TermList*> toDo(8);
  static Stack<int> toDoIndex(8);
  static Stack<Term*> terms(8);
  static Stack<VarSpec> termRefVars(8);
  static Stack<TermList> args(8);
  static DHMap<VarSpec, TermList, VarSpec::Hash1, VarSpec::Hash2> known;

  //is inserted into termRefVars, if respective
  //term in terms isn't referenced by any variable
  const VarSpec nilVS(-1,0);

  toDo.push(&trm);
  toDoIndex.push(index);

  while(!toDo.isEmpty()) {
    TermList* tt=toDo.pop();
    index=toDoIndex.pop();
    if(tt->isEmpty()) {
      Term* orig=terms.pop();
      //here we assume, that stack is an array with
      //second topmost element as &top()-1, third at
      //&top()-2, etc...
      TermList* argLst=&args.top() - (orig->arity()-1);
      args.truncate(args.length() - orig->arity());
      TermList constructed;
      constructed.setTerm(Term::create(orig,argLst));
      args.push(constructed);

      VarSpec ref=termRefVars.pop();
      if(ref!=nilVS) {
	ALWAYS(known.insert(ref,constructed));
      }
      continue;
    } else {
      //if tt==&trm, we're dealing with the top
      //term, for which the next() is undefined
      if(tt!=&trm) {
	toDo.push(tt->next());
	toDoIndex.push(index);
      }
    }

    TermSpec ts(*tt,index);

    VarSpec vs;
    if(ts.term.isVar()) {
      vs=root(getVarSpec(ts));

      TermList found;
      if(known.find(vs, found)) {
	args.push(found);
	continue;
      }

      ts=deref(vs);
      if(ts.term.isVar()) {
	ASS(ts.index==UNBOUND_INDEX);
	args.push(ts.term);
	continue;
      }
    } else {
      vs=nilVS;
    }
    Term* t=ts.term.term();
    if(t->shared() && t->ground()) {
      args.push(ts.term);
      continue;
    }
    terms.push(t);
    termRefVars.push(vs);

    toDo.push(t->args());
    toDoIndex.push(ts.index);
  }
  ASS(toDo.isEmpty() && toDoIndex.isEmpty() && terms.isEmpty() && args.length()==1);
  known.reset();
  return args.pop();
}

size_t RobSubstitution::getApplicationResultWeight(TermList trm, int index) const
{
  CALL("RobSubstitution::getApplicationResultWeight");

  static Stack<TermList*> toDo(8);
  static Stack<int> toDoIndex(8);
  static Stack<Term*> terms(8);
  static Stack<VarSpec> termRefVars(8);
  static Stack<size_t> argSizes(8);

  static DHMap<VarSpec, size_t, VarSpec::Hash1, VarSpec::Hash2> known;
  known.reset();

  //is inserted into termRefVars, if respective
  //term in terms isn't referenced by any variable
  const VarSpec nilVS(-1,0);

  toDo.push(&trm);
  toDoIndex.push(index);

  while(!toDo.isEmpty()) {
    TermList* tt=toDo.pop();
    index=toDoIndex.pop();
    if(tt->isEmpty()) {
      Term* orig=terms.pop();
      unsigned arity = orig->arity();
      //here we assume, that stack is an array with
      //second topmost element as &top()-1, third at
      //&top()-2, etc...
      size_t* szArr=&argSizes.top() - (orig->arity()-1);
      size_t sz = 1; //1 for the function symbol
      for(unsigned i=0; i<arity; i++) {
	sz += szArr[i];
      }
      argSizes.truncate(argSizes.length() - arity);
      argSizes.push(sz);

      VarSpec ref=termRefVars.pop();
      if(ref!=nilVS) {
	ALWAYS(known.insert(ref,sz));
      }
      continue;
    } else {
      //if tt==&trm, we're dealing with the top
      //term, for which the next() is undefined
      if(tt!=&trm) {
	toDo.push(tt->next());
	toDoIndex.push(index);
      }
    }

    TermSpec ts(*tt,index);

    VarSpec vs;
    if(ts.term.isVar()) {
      vs=root(getVarSpec(ts));

      size_t found;
      if(known.find(vs, found)) {
	argSizes.push(found);
	continue;
      }

      ts=deref(vs);
      if(ts.term.isVar()) {
	ASS(ts.index==UNBOUND_INDEX);
	argSizes.push(1);
	continue;
      }
    } else {
      vs=nilVS;
    }
    Term* t=ts.term.term();
    if(t->shared() && t->ground()) {
      argSizes.push(t->weight());
      continue;
    }
    terms.push(t);
    termRefVars.push(vs);

    toDo.push(t->args());
    toDoIndex.push(ts.index);
  }
  ASS(toDo.isEmpty() && toDoIndex.isEmpty() && terms.isEmpty() && argSizes.length()==1);
  return argSizes.pop();
}

size_t RobSubstitution::getApplicationResultWeight(Literal* lit, int index) const
{
  CALL("RobSubstitution::getApplicationResultWeight");
  static DArray<TermList> ts(32);

  if (lit->ground()) {
    return lit->weight();
  }

  size_t res = 1; //the predicate symbol weight
  for (TermList* args = lit->args(); ! args->isEmpty(); args = args->next()) {
    size_t argWeight = getApplicationResultWeight(*args,index);
    res += argWeight;
  }
#if VDEBUG && DEBUG_RESULT_WEIGHT_COMPUTATION
  ASS_REP2(apply(lit, index)->weight()==res, res, lit->toString()+"   "+apply(lit, index)->toString());
#endif
  return res;
}


/**
 * Return iterator on matching substitutions of @b l1 and @b l2.
 *
 * For guides on use of the iterator, see the documentation of
 * RobSubstitution::AssocIterator.
 */
SubstIterator RobSubstitution::matches(Literal* base, int baseIndex,
	Literal* instance, int instanceIndex, bool complementary)
{
  return getAssocIterator<MatchingFn>(this, base, baseIndex,
	  instance, instanceIndex, complementary);
}

/**
 * Return iterator on unifying substitutions of @b l1 and @b l2.
 *
 * For guides on use of the iterator, see the documentation of
 * RobSubstitution::AssocIterator.
 */
SubstIterator RobSubstitution::unifiers(Literal* l1, int l1Index,
	Literal* l2, int l2Index, bool complementary)
{
  return getAssocIterator<UnificationFn>(this, l1, l1Index,
	  l2, l2Index, complementary);
}

template<class Fn>
SubstIterator RobSubstitution::getAssocIterator(RobSubstitution* subst,
	  Literal* l1, int l1Index, Literal* l2, int l2Index, bool complementary)
{
  CALL("RobSubstitution::getAssocIterator");

  if( !Literal::headersMatch(l1,l2,complementary) ) {
    // We also get here if the sorts of equality literals do not match.
    return SubstIterator::getEmpty();
  }

  if( !l1->commutative() ) {
    return pvi( getContextualIterator(getSingletonIterator(subst),
	    AssocContext<Fn>(l1, l1Index, l2, l2Index)) );
  } else {
    return vi(
	    new AssocIterator<Fn>(subst, l1, l1Index, l2, l2Index));
  }
}

template<class Fn>
struct RobSubstitution::AssocContext
{
  AssocContext(Literal* l1, int l1Index, Literal* l2, int l2Index)
  : _l1(l1), _l1i(l1Index), _l2(l2), _l2i(l2Index) {}
  bool enter(RobSubstitution* subst)
  {
    subst->bdRecord(_bdata);
    bool res=Fn::associate(subst, _l1, _l1i, _l2, _l2i);
    if(!res) {
      subst->bdDone();
      ASS(_bdata.isEmpty());
    }
    return res;
  }
  void leave(RobSubstitution* subst)
  {
    subst->bdDone();
    _bdata.backtrack();
  }
private:
  Literal* _l1;
  int _l1i;
  Literal* _l2;
  int _l2i;
  bool _complementary;
  BacktrackData _bdata;
};

/**
 * Iterator on associating[1] substitutions of two literals.
 *
 * Using this iterator requires special care, as the
 * substitution being returned is always the same object.
 * The rules for safe use are:
 * - After the iterator is created and before it's
 * destroyed, or hasNext() gives result false, the original
 * substitution is invalid.
 * - Substitution retrieved by call to the method next()
 * is valid only until the hasNext() method is called again
 * (or until the iterator is destroyed).
 * - Before each call to next(), hasNext() has to be called at
 * least once.
 *
 * There rules are quite natural, and the 3rd one is
 * required by many other iterators as well.
 *
 * Template parameter class Fn has to contain following
 * methods:
 * bool associate(RobSubstitution*, Literal* l1, int l1Index,
 * 	Literal* l2, int l2Index, bool complementary)
 * bool associate(RobSubstitution*, TermList t1, int t1Index,
 * 	TermList t2, int t2Index)
 * There is supposed to be one Fn class for unification and
 * one for matching.
 *
 * [1] associate means either match or unify
 */
template<class Fn>
class RobSubstitution::AssocIterator
:public IteratorCore<RobSubstitution*>
{
public:
  AssocIterator(RobSubstitution* subst, Literal* l1, int l1Index,
	  Literal* l2, int l2Index)
  : _subst(subst), _l1(l1), _l1i(l1Index), _l2(l2),
  _l2i(l2Index), _state(FIRST), _used(true)
  {
    ASS_EQ(_l1->functor(),_l2->functor());
    ASS(_l1->commutative());
    ASS_EQ(_l1->arity(),2);
  }
  ~AssocIterator()
  {
    CALL("RobSubstitution::AssocIterator::~AssocIterator");

    if(_state!=FINISHED && _state!=FIRST) {
	backtrack();
    }
    ASS(_bdata.isEmpty());
  }
  bool hasNext()
  {
    CALL("RobSubstitution::AssocIterator::hasNext");

    if(_state==FINISHED) {
      return false;
    }
    if(!_used) {
      return true;
    }
    _used=false;

    if(_state!=FIRST) {
      backtrack();
    }
    _subst->bdRecord(_bdata);

    switch(_state) {
    case NEXT_STRAIGHT:
      if(Fn::associate(_subst, _l1, _l1i, _l2, _l2i)) {
	_state=NEXT_REVERSED;
	break;
      }
      //no break here intentionally
    case NEXT_REVERSED:
    {
      TermList t11=*_l1->nthArgument(0);
      TermList t12=*_l1->nthArgument(1);
      TermList t21=*_l2->nthArgument(0);
      TermList t22=*_l2->nthArgument(1);
      if(Fn::associate(_subst, t11, _l1i, t22, _l2i)) {
	if(Fn::associate(_subst, t12, _l1i, t21, _l2i)) {
	  _state=NEXT_CLEANUP;
	  break;
	}
	//the first successful association will be undone
	//in case NEXT_CLEANUP
      }
    }
    //no break here intentionally
    case NEXT_CLEANUP:
      //undo the previous match
      backtrack();
      _state=FINISHED;
#if VDEBUG
      break;
    default:
      ASSERTION_VIOLATION;
#endif
    }
    ASS(_state!=FINISHED || _bdata.isEmpty());
    return _state!=FINISHED;
  }

  RobSubstitution* next()
  {
    _used=true;
    return _subst;
  }
private:
  void backtrack()
  {
    CALL("RobSubstitution::AssocIterator::backtrack");

    ASS_EQ(&_bdata,&_subst->bdGet());
    _subst->bdDone();
    _bdata.backtrack();
  }
  enum State {
    FIRST=0,
    NEXT_STRAIGHT=0,
    NEXT_REVERSED=1,
    NEXT_CLEANUP=2,
    FINISHED=3
  };

  RobSubstitution* _subst;
  Literal* _l1;
  int _l1i;
  Literal* _l2;
  int _l2i;
  BacktrackData _bdata;

  State _state;
  /**
   * true if the current substitution have already been
   * retrieved by the next() method, or if there isn't
   * any (hasNext() hasn't been called yet)
   */
  bool _used;
};
struct RobSubstitution::MatchingFn {
  static bool associate(RobSubstitution* subst, Literal* l1, int l1Index,
	  Literal* l2, int l2Index)
  { return subst->matchArgs(l1,l1Index,l2,l2Index); }

  static bool associate(RobSubstitution* subst, TermList t1, int t1Index,
	  TermList t2, int t2Index)
  { return subst->match(t1,t1Index,t2,t2Index); }
};
struct RobSubstitution::UnificationFn {
  static bool associate(RobSubstitution* subst, Literal* l1, int l1Index,
	  Literal* l2, int l2Index)
  { return subst->unifyArgs(l1,l1Index,l2,l2Index); }

  static bool associate(RobSubstitution* subst, TermList t1, int t1Index,
	  TermList t2, int t2Index)
  { return subst->unify(t1,t1Index,t2,t2Index); }
};


#if VDEBUG
vstring RobSubstitution::toString(bool deref) const
{
  CALL("RobSubstitution::toString");
  vstring res;
  BankType::Iterator bit(_bank);
  while(bit.hasNext()) {
    VarSpec v;
    TermSpec binding;
    bit.next(v,binding);
    TermList tl;
    if(v.index==SPECIAL_INDEX) {
      res+="S"+Int::toString(v.var)+" -> ";
      tl.makeSpecialVar(v.var);
    } else {
      res+="X"+Int::toString(v.var)+"/"+Int::toString(v.index)+ " -> ";
      tl.makeVar(v.var);
    }
    if(deref) {
      tl=apply(tl, v.index);
      res+=tl.toString()+"\n";
    } else {
      res+=binding.term.toString()+"/"+Int::toString(binding.index)+"\n";
    }

  }
  return res;
}

vstring RobSubstitution::VarSpec::toString() const
{
  if(index==SPECIAL_INDEX) {
    return "S"+Int::toString(var);
  } else {
    return "X"+Int::toString(var)+"/"+Int::toString(index);
  }
}

vstring RobSubstitution::TermSpec::toString() const
{
  return term.toString()+"/"+Int::toString(index);
}

ostream& operator<< (ostream& out, RobSubstitution::VarSpec vs )
{
  return out<<vs.toString();
}

ostream& operator<< (ostream& out, RobSubstitution::TermSpec ts )
{
  return out<<ts.toString();
}

#endif

/**
 * First hash function for DHMap.
 */
unsigned RobSubstitution::VarSpec::Hash1::hash(VarSpec& o, int capacity)
{
//  return o.var + o.index*(capacity>>1) + (o.index>>1)*(capacity>>3);
//  return o.var^(o.var/capacity) + o.index*(capacity>>1) + (o.index>bv>2)*(capacity>>3);
//This might work better

  int res=(o.var%(capacity<<1) - capacity);
  if(res<0)
    //this turns x into -x-1
    res = ~res;
  if(o.index&1)
    return static_cast<unsigned>(-res+capacity-o.index);
  else
    return static_cast<unsigned>(res);
}

/**
 * Second hash function for DHMap. It just uses the hash function from Lib::Hash
 */
unsigned RobSubstitution::VarSpec::Hash2::hash(VarSpec& o)
{
  return Lib::Hash::hash(reinterpret_cast<const unsigned char*>(&o), sizeof(VarSpec));
//  return o.var+o.index;
}

}// namespace Kernel
