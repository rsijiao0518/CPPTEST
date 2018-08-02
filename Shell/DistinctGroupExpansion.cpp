
/*
 * File DistinctGroupExpansion.cpp.
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
 * @file DistinctGroupExpansion.cpp
 * Expands distinct groups
 * @since 18/03/2015 Manchester
 * @author Giles
 */

#include "Lib/Environment.hpp"
#include "Lib/Stack.hpp"
#include "Lib/List.hpp"

#include "Kernel/Signature.hpp"
#include "Kernel/Problem.hpp"
#include "Kernel/Unit.hpp"
#include "Kernel/Term.hpp"
#include "Kernel/Formula.hpp"
#include "Kernel/FormulaUnit.hpp"
#include "Kernel/Inference.hpp"
#include "Kernel/SortHelper.hpp"
#include "Kernel/Connective.hpp"

#include "Options.hpp"
#include "DistinctGroupExpansion.hpp"

using namespace Shell;

/**
 * TODO check problem invalidation
 */
void DistinctGroupExpansion::apply(Problem& prb)
{
  CALL("DistinctGroupExpansion::apply(Problem&)");

  if(apply(prb.units())){
    prb.invalidateProperty();
    prb.reportFormulasAdded();
    prb.reportEqualityAdded(false); // Do we need to do this if adding disequality?
  }

}

/**
 * Attempts to expand each recorded distinct group
 * (this includes those for builtin sorts i.e. ints, strings...)
 * If all groups are expanded we indicate there are no distinct groups left, which will
 * prevent the distinct simplifier being added later
 */
bool DistinctGroupExpansion::apply(UnitList*& units)
{
  CALL("DistinctGroupExpansion::apply(UnitList*&)");

  bool added=false;

  Stack<Stack<unsigned>*> group_members = env.signature->getDistinctGroupMembers();

  // If this is updated then make sure you update the check in
  // Kernel::Signature::Symol::addToDistinctGroup as well
  bool expandEverything = 
    env.options->saturationAlgorithm()==Options::SaturationAlgorithm::FINITE_MODEL_BUILDING ||
    env.options->bfnt();

  bool someLeft = false;

  for(unsigned i=0;i<group_members.size();i++){
    Stack<unsigned>* members = group_members[i];
    if(members->size() > 0) {
      if( members->size()>1 && (members->size() <= EXPAND_UP_TO_SIZE || expandEverything)) {
        added=true;
        Formula* expansion = expand(*members);
        if(env.options->showPreprocessing()){
          env.out() << "  expansion adding " << expansion->toString() << endl;
        }
        // Currently we just say that these are from the Input, not $distinct or theory of ints
        UnitList::push(new FormulaUnit(expansion, new Inference(Inference::INPUT),Unit::AXIOM),units);
      }
      else someLeft=true;
    }
  } 

  if(!someLeft){
    env.signature->noDistinctGroupsLeft();
  }

  return added;
}

/**
 * If a distinct group of constants has 2 members then a single disequality is creatd
 * Otherwise a conjunction of disequalities is created
 */
Formula* DistinctGroupExpansion::expand(Stack<unsigned>& constants)
{
  CALL("DistinctGroupExpansion::expand");

  ASS(constants.size()>=2);
  // If there are 2 just create a disequality
  if(constants.size()==2){
    TermList a = TermList(Term::createConstant(constants[0]));
    TermList b = TermList(Term::createConstant(constants[1]));
    unsigned sort = SortHelper::getResultSort(a.term());
    return new AtomicFormula(Literal::createEquality(false,a,b,sort));
  }

  // Otherwise create a formula list of disequalities
  FormulaList* diseqs = 0; 

  for(unsigned i=0;i<constants.size();i++){
    TermList a = TermList(Term::createConstant(constants[i]));
    ASS(a.isSafe());
    unsigned sort = SortHelper::getResultSort(a.term());

    for(unsigned j=0;j<i;j++){
      TermList b = TermList(Term::createConstant(constants[j]));
      ASS(b.isSafe());
      
      Formula* new_dis = new AtomicFormula(Literal::createEquality(false,a,b,sort));
      if(diseqs) FormulaList::push(new_dis,diseqs);
      else diseqs = new FormulaList(new_dis);

    }
  }

  // and create an AND junction of these
  return new JunctionFormula(Connective::AND, diseqs);

}

