/*
 * File Induction 
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
 * @file Induction.cpp
 * Implements class Induction.
 */

#include "Debug/RuntimeStatistics.hpp"

#include "Lib/Environment.hpp"
#include "Lib/Set.hpp"
#include "Lib/Array.hpp"

#include "Kernel/TermIterators.hpp"
#include "Kernel/Signature.hpp"
#include "Kernel/Clause.hpp"
#include "Kernel/Unit.hpp"
#include "Kernel/Inference.hpp"
#include "Kernel/Sorts.hpp"
#include "Kernel/Theory.hpp"
#include "Kernel/Formula.hpp"
#include "Kernel/FormulaUnit.hpp"
#include "Kernel/Connective.hpp"
#include "Kernel/RobSubstitution.hpp"

#include "Saturation/SaturationAlgorithm.hpp"

#include "Shell/Options.hpp"
#include "Shell/Statistics.hpp"
#include "Shell/NewCNF.hpp"
#include "Shell/NNF.hpp"
#include "Shell/Rectify.hpp"

#include "Indexing/Index.hpp"
#include "Indexing/ResultSubstitution.hpp"
#include "Inferences/BinaryResolution.hpp"

#include "Induction.hpp"

namespace Inferences
{
using namespace Kernel;
using namespace Lib; 


TermList TermReplacement::transformSubterm(TermList trm)
{
  CALL("TermReplacement::transformSubterm");

  if(trm.isTerm() && trm.term()==_o){
   return _r;
  }
  return trm;
}

ClauseIterator Induction::generateClauses(Clause* premise)
{
  CALL("Induction::generateClauses");

  return pvi(InductionClauseIterator(premise));
}

InductionClauseIterator::InductionClauseIterator(Clause* premise)
{
  CALL("InductionClauseIterator::InductionClauseIterator");

  static Options::InductionChoice kind = env.options->inductionChoice();
  static bool all = (kind == Options::InductionChoice::ALL);
  static bool goal = (kind == Options::InductionChoice::GOAL);
  static bool goal_plus = (kind == Options::InductionChoice::GOAL_PLUS);
  static unsigned maxD = env.options->maxInductionDepth();
  static bool unitOnly = env.options->inductionUnitOnly();


  if((!unitOnly || premise->length()==1) && 
     (all || ( (goal || goal_plus) && premise->isGoal())) &&
     (maxD == 0 || premise->inductionDepth() < maxD)
    )
  {
    for(unsigned i=0;i<premise->length();i++){
      process(premise,(*premise)[i]);
    }
  }
}


void InductionClauseIterator::process(Clause* premise, Literal* lit)
{
  CALL("Induction::ClauseIterator::process");

  //cout << "PROCESS " << premise->toString() << endl;

  static Options::InductionChoice kind = env.options->inductionChoice();
  static bool all = (kind == Options::InductionChoice::ALL);
  static bool goal_plus = (kind == Options::InductionChoice::GOAL_PLUS);
  static bool negOnly = env.options->inductionNegOnly();
  static bool structInd = env.options->induction() == Options::Induction::BOTH ||
                         env.options->induction() == Options::Induction::STRUCTURAL;
  static bool mathInd = env.options->induction() == Options::Induction::BOTH ||
                         env.options->induction() == Options::Induction::MATHEMATICAL;

  if((!negOnly || lit->isNegative() || 
         (theory->isInterpretedPredicate(lit) && theory->isInequality(theory->interpretPredicate(lit)))
       )&& 
       lit->ground()
      ){

      Set<Term*> ta_terms;
      Set<Term*> int_terms;
      SubtermIterator it(lit);
      it.next(); // to move past the lit symbol
      while(it.hasNext()){
        TermList ts = it.next();
        if(!ts.term()){ continue; }
        unsigned f = ts.term()->functor(); 
        if(env.signature->functionArity(f)==0 &&
           (
               all
            || env.signature->getFunction(f)->inGoal()
            || (goal_plus && env.signature->getFunction(f)->inductionSkolem()) // set in NewCNF
           )
        ){
         if(structInd && 
            env.signature->isTermAlgebraSort(env.signature->getFunction(f)->fnType()->result()) &&
            !env.signature->getFunction(f)->termAlgebraCons()
           ){
            ta_terms.insert(ts.term());
          }
          if(mathInd && 
             env.signature->getFunction(f)->fnType()->result()==Sorts::SRT_INTEGER &&
             !theory->isInterpretedConstant(f)
            ){
            int_terms.insert(ts.term());
          }
        }
      }
      Set<Term*>::Iterator citer1(int_terms);
      while(citer1.hasNext()){
        Term* t = citer1.next();
        static bool one = env.options->mathInduction() == Options::MathInductionKind::ONE ||
                          env.options->mathInduction() == Options::MathInductionKind::ALL;
        static bool two = env.options->mathInduction() == Options::MathInductionKind::TWO ||
                          env.options->mathInduction() == Options::MathInductionKind::ALL;
        if(notDone(lit,t)){
          if(one){
            performMathInductionOne(premise,lit,t);
          }
          if(two){
            performMathInductionTwo(premise,lit,t);
          }
        }
      }
      Set<Term*>::Iterator citer2(ta_terms);
      while(citer2.hasNext()){
        Term* t = citer2.next();
        //cout << "PERFORM INDUCTION on " << env.signature->functionName(c) << endl;
        static bool one = env.options->structInduction() == Options::StructuralInductionKind::ONE ||
                          env.options->structInduction() == Options::StructuralInductionKind::ALL; 
        static bool two = env.options->structInduction() == Options::StructuralInductionKind::TWO ||
                          env.options->structInduction() == Options::StructuralInductionKind::ALL; 
        static bool three = env.options->structInduction() == Options::StructuralInductionKind::THREE ||
                          env.options->structInduction() == Options::StructuralInductionKind::ALL;

        if(notDone(lit,t)){

          if(one){
            performStructInductionOne(premise,lit,t);
          }
          if(two){
            performStructInductionTwo(premise,lit,t);
          }
          if(three){
            performStructInductionThree(premise,lit,t);
          }
        }
      } 
   }
}

      // deal with integer constants using two hypotheses
      // (L[0] & (![X] : (X>=0 & L[X]) -> L[x+1])) -> (![Y] : Y>=0 -> L[Y])
      // (L[0] & (![X] : (X<=0 & L[X]) -> L[x-1])) -> (![Y] : Y<=0 -> L[Y])
      // for some ~L[a]
void InductionClauseIterator::performMathInductionOne(Clause* premise, Literal* lit, Term* term) 
{
  CALL("InductionClauseIterator::performMathInductionOne");

        //cout << "PERFORM INDUCTION on " << env.signature->functionName(c) << endl;

        TermList zero(theory->representConstant(IntegerConstantType(0)));
        TermList one(theory->representConstant(IntegerConstantType(1)));
        TermList mone(theory->representConstant(IntegerConstantType(-1)));

        TermList x(0,false);
        TermList y(1,false);

        Literal* clit = Literal::complementaryLiteral(lit);

        // create L[zero]
        TermReplacement cr1(term,zero);
        Formula* Lzero = new AtomicFormula(cr1.transform(clit));

        // create L[X] 
        TermReplacement cr2(term,x);
        Formula* Lx = new AtomicFormula(cr2.transform(clit));

        // create L[Y] 
        TermReplacement cr3(term,y);
        Formula* Ly = new AtomicFormula(cr3.transform(clit));

        // create L[X+1] 
        TermList fpo(Term::create2(env.signature->getInterpretingSymbol(Theory::INT_PLUS),x,one));
        TermReplacement cr4(term,fpo);
        Formula* Lxpo = new AtomicFormula(cr4.transform(clit));

        // create L[X-1]
        TermList fmo(Term::create2(env.signature->getInterpretingSymbol(Theory::INT_PLUS),x,mone));
        TermReplacement cr5(term,fmo);
        Formula* Lxmo = new AtomicFormula(cr5.transform(clit));

        // create X>=0, which is ~X<0
        Formula* Lxgz = new AtomicFormula(Literal::create2(env.signature->getInterpretingSymbol(Theory::INT_LESS),
                                         false,x,zero));
        // create Y>=0, which is ~Y<0
        Formula* Lygz = new AtomicFormula(Literal::create2(env.signature->getInterpretingSymbol(Theory::INT_LESS),
                                         false,y,zero));
        // create X<=0, which is ~0<X
        Formula* Lxlz = new AtomicFormula(Literal::create2(env.signature->getInterpretingSymbol(Theory::INT_LESS),
                                         false,zero,x));
        // create Y<=0, which is ~0<Y
        Formula* Lylz = new AtomicFormula(Literal::create2(env.signature->getInterpretingSymbol(Theory::INT_LESS),
                                         false,zero,y));


        // (L[0] & (![X] : (X>=0 & L[X]) -> L[x+1])) -> (![Y] : Y>=0 -> L[Y])

        Formula* hyp1 = new BinaryFormula(Connective::IMP,
                          new JunctionFormula(Connective::AND,new FormulaList(Lzero,new FormulaList(
                            Formula::quantify(new BinaryFormula(Connective::IMP,
                              new JunctionFormula(Connective::AND, new FormulaList(Lxgz,new FormulaList(Lx,0))),
                              Lxpo)) 
                          ,0))),
                          Formula::quantify(new BinaryFormula(Connective::IMP,Lygz,Ly)));

        // (L[0] & (![X] : (X<=0 & L[X]) -> L[x-1])) -> (![Y] : Y<=0 -> L[Y])

        Formula* hyp2 = new BinaryFormula(Connective::IMP,
                          new JunctionFormula(Connective::AND,new FormulaList(Lzero,new FormulaList(
                            Formula::quantify(new BinaryFormula(Connective::IMP,
                              new JunctionFormula(Connective::AND, new FormulaList(Lxlz,new FormulaList(Lx,0))),
                              Lxmo))
                          ,0))),
                          Formula::quantify(new BinaryFormula(Connective::IMP,Lylz,Ly)));
  
        NewCNF cnf(0);
        cnf.setForInduction();
        Stack<Clause*> hyp_clauses;
        FormulaUnit* fu1 = new FormulaUnit(hyp1,new Inference(Inference::INDUCTION),Unit::AXIOM);
        FormulaUnit* fu2 = new FormulaUnit(hyp2,new Inference(Inference::INDUCTION),Unit::AXIOM);
        cnf.clausify(NNF::ennf(fu1), hyp_clauses);
        cnf.clausify(NNF::ennf(fu2), hyp_clauses);

        // Now perform resolution between lit and the hyp_clauses on clit, which should be contained in each clause!
        Stack<Clause*>::Iterator cit(hyp_clauses);
        while(cit.hasNext()){
          Clause* c = cit.next();
          //TODO destroy this?
          RobSubstitution* subst = new RobSubstitution();
          subst->unify(TermList(lit),0,TermList(Ly->literal()),1);
          SLQueryResult qr(lit,premise,ResultSubstitution::fromSubstitution(subst,1,0));
          Clause* r = BinaryResolution::generateClause(c,Ly->literal(),qr,*env.options);
          _clauses.push(r);
        }
        env.statistics->induction++;
 }

void InductionClauseIterator::performMathInductionTwo(Clause* premise, Literal* lit, Term* term) 
{
  CALL("InductionClauseIterator::performMathInductionTwo");

  NOT_IMPLEMENTED;
}

/**
 * Introduce the Induction Hypothesis
 * ( L[base1] & ... & L[basen] & (L[x] => L[c1(x)]) & ... (L[x] => L[cm(x)]) ) => L[x]
 * for some lit ~L[a]
 * and then force binary resolution on L for each resultant clause
 */

void InductionClauseIterator::performStructInductionOne(Clause* premise, Literal* lit, Term* term) 
{
  CALL("InductionClauseIterator::performStructInductionOne"); 

  TermAlgebra* ta = env.signature->getTermAlgebraOfSort(env.signature->getFunction(term->functor())->fnType()->result());
  unsigned ta_sort = ta->sort();

  FormulaList* formulas = FormulaList::empty();

  Literal* clit = Literal::complementaryLiteral(lit);
  unsigned var = 0;

  // first produce the formula
  for(unsigned i=0;i<ta->nConstructors();i++){
    TermAlgebraConstructor* con = ta->constructor(i);
    unsigned arity = con->arity();
    Formula* f = 0;

    // non recursive get L[_]
    if(!con->recursive()){
      if(arity==0){
        TermReplacement cr(term,TermList(Term::createConstant(con->functor())));
        f = new AtomicFormula(cr.transform(clit)); 
      }
      else{
        Stack<TermList> argTerms;
        for(unsigned i=0;i<arity;i++){
          argTerms.push(TermList(var,false));
          var++;
        }
        TermReplacement cr(term,TermList(Term::create(con->functor(),(unsigned)argTerms.size(), argTerms.begin())));
        f = new AtomicFormula(cr.transform(clit));
      }
    }
    // recursive get (L[x] => L[c(x)])
    else{
      ASS(arity>0);
      Stack<TermList> argTerms;
      Stack<TermList> ta_vars;
      for(unsigned i=0;i<arity;i++){
        TermList x(var,false);
        var++;
        if(con->argSort(i) == ta_sort){
          ta_vars.push(x);
        }
        argTerms.push(x);
      }
      TermReplacement cr(term,TermList(Term::create(con->functor(),(unsigned)argTerms.size(), argTerms.begin())));
      Formula* right = new AtomicFormula(cr.transform(clit));
      Formula* left = 0;
      ASS(ta_vars.size()>=1);
      if(ta_vars.size()==1){
        TermReplacement cr(term,ta_vars[0]);
        left = new AtomicFormula(cr.transform(clit));
      }
      else{
        FormulaList* args = FormulaList::empty();
        Stack<TermList>::Iterator tvit(ta_vars);
        while(tvit.hasNext()){
          TermReplacement cr(term,tvit.next());
          args = new FormulaList(new AtomicFormula(cr.transform(clit)),args);
        }
        left = new JunctionFormula(Connective::AND,args);
      }
      f = new BinaryFormula(Connective::IMP,left,right);
    }

    ASS(f);
    formulas = new FormulaList(f,formulas);
  }
  TermReplacement cr(term,TermList(var,false));
  Literal* conclusion = cr.transform(clit);
  Formula* hypothesis = new BinaryFormula(Connective::IMP,
                            Formula::quantify(new JunctionFormula(Connective::AND,formulas)),
                            Formula::quantify(new AtomicFormula(conclusion)));

  NewCNF cnf(0);
  cnf.setForInduction();
  Stack<Clause*> hyp_clauses;
  FormulaUnit* fu = new FormulaUnit(hypothesis,new Inference(Inference::INDUCTION),Unit::AXIOM);
  cnf.clausify(NNF::ennf(fu), hyp_clauses);

  //cout << "Clausify " << fu->toString() << endl;

  // Now perform resolution between lit and the hyp_clauses on clit, which should be contained in each clause!
  Stack<Clause*>::Iterator cit(hyp_clauses);
  while(cit.hasNext()){
    Clause* c = cit.next();
    static ResultSubstitutionSP identity = ResultSubstitutionSP(new IdentitySubstitution());
    SLQueryResult qr(lit,premise,identity);
    Clause* r = BinaryResolution::generateClause(c,conclusion,qr,*env.options);
    _clauses.push(r);
  }
  env.statistics->induction++;
}

/**
 * This idea (taken from the CVC4 paper) is that there exists some smallest k that makes lit true
 * We produce the clause ~L[x] \/ ?y : L[y] & !z (z subterm y -> ~L[z])
 * and perform resolution with lit L[c]
 */
void InductionClauseIterator::performStructInductionTwo(Clause* premise, Literal* lit, Term* term) 
{
  //cout << "TWO " << premise->toString() << endl;

  TermAlgebra* ta = env.signature->getTermAlgebraOfSort(env.signature->getFunction(term->functor())->fnType()->result());
  unsigned ta_sort = ta->sort();

  Literal* clit = Literal::complementaryLiteral(lit);

  // make L[y]
  TermList y(0,false); 
  TermReplacement cr(term,y);
  Literal* Ly = cr.transform(lit);

  // for each constructor and destructor make
  // ![Z] : y = cons(Z,dec(y)) -> ( ~L[dec1(y)] & ~L[dec2(y)]
  FormulaList* formulas = FormulaList::empty();

  for(unsigned i=0;i<ta->nConstructors();i++){
    TermAlgebraConstructor* con = ta->constructor(i);
    //cout << env.signature->functionName(con->functor()) << endl;
    unsigned arity = con->arity();
  
    // ignore a constructor if it doesn't mention ta_sort
    bool ignore = (arity == 0);
    if(!ignore){
      ignore=true;
      for(unsigned j=0;j<arity; j++){ 
        if(con->argSort(j)==ta_sort){           
          ignore=false;
        }
      }
    }

    if(!ignore){
  
      // First generate all argTerms and remember those that are of sort ta_sort 
      Stack<TermList> argTerms;
      Stack<TermList> taTerms; 
      for(unsigned j=0;j<arity;j++){
        unsigned dj = con->destructorFunctor(j);
        TermList djy(Term::create1(dj,y));
        argTerms.push(djy);
        if(con->argSort(j) == ta_sort){
          taTerms.push(djy);
        }
      }
      // create y = con1(...d1(y)...d2(y)...)
      TermList coni(Term::create(con->functor(),(unsigned)argTerms.size(), argTerms.begin()));
      Literal* kneq = Literal::createEquality(true,y,coni,ta_sort);
      FormulaList* And = FormulaList::empty(); 
      Stack<TermList>::Iterator tit(taTerms);
      unsigned and_terms = 0;
      while(tit.hasNext()){
        TermList djy = tit.next();
        TermReplacement(term,djy);
        Literal* nLdjy = cr.transform(clit);
        Formula* f = new AtomicFormula(nLdjy); 
        And = new FormulaList(f,And);
        and_terms++;
      }
      ASS(and_terms>0);
      Formula* imp = new BinaryFormula(Connective::IMP,
                            new AtomicFormula(kneq),
                            (and_terms>1) ? new JunctionFormula(Connective::AND,And)
                                          : And->head()
                            );
      formulas = new FormulaList(imp,formulas);
      
    }
  }
  Formula* exists = new QuantifiedFormula(Connective::EXISTS, new Formula::VarList(y.var(),0),0,
                  new JunctionFormula(Connective::AND,new FormulaList(new AtomicFormula(Ly),formulas))); 
  
  TermReplacement cr2(term,TermList(1,false));
  Literal* conclusion = cr2.transform(clit);
  FormulaList* orf = new FormulaList(exists,new FormulaList(Formula::quantify(new AtomicFormula(conclusion)),FormulaList::empty()));
  Formula* hypothesis = new JunctionFormula(Connective::OR,orf);

  NewCNF cnf(0);
  cnf.setForInduction();
  Stack<Clause*> hyp_clauses;
  FormulaUnit* fu = new FormulaUnit(hypothesis,new Inference(Inference::INDUCTION),Unit::AXIOM);
  cnf.clausify(NNF::ennf(fu), hyp_clauses);

  //cout << "Clausify " << fu->toString() << endl;

  // Now perform resolution between lit and the hyp_clauses on clit, which should be contained in each clause!
  Stack<Clause*>::Iterator cit(hyp_clauses);
  while(cit.hasNext()){
    Clause* c = cit.next();
    static ResultSubstitutionSP identity = ResultSubstitutionSP(new IdentitySubstitution());
    SLQueryResult qr(lit,premise,identity);
    Clause* r = BinaryResolution::generateClause(c,conclusion,qr,*env.options);
    _clauses.push(r);
  }
  env.statistics->induction++;  

}

/*
 * A variant of Two where we are stronger with respect to all subterms. here the existential part is
 *
 * ?y : L[y] &_{con_i} ( y = con_i(..dec(y)..) -> smaller(dec(y))) 
             & (!x : smallerThanY(x) -> smallerThanY(destructor(x))) 
             & !z : smallerThanY(z) => ~L[z]
 *
 * i.e. we add a new special predicat that is true when its argument is smaller than Y
 *
 */
void InductionClauseIterator::performStructInductionThree(Clause* premise, Literal* lit, Term* term) 
{
  CALL("InductionClauseIterator::performStructInductionThree");


  TermAlgebra* ta = env.signature->getTermAlgebraOfSort(env.signature->getFunction(term->functor())->fnType()->result());
  unsigned ta_sort = ta->sort();

  Literal* clit = Literal::complementaryLiteral(lit);

  // make L[y]
  TermList x(0,false); 
  TermList y(1,false); 
  TermList z(2,false); 
  TermReplacement cr(term,y);
  Literal* Ly = cr.transform(lit);

  // make smallerThanY
  unsigned sty = env.signature->addFreshPredicate(1,"smallerThan");
  env.signature->getPredicate(sty)->setType(OperatorType::getPredicateType({ta_sort}));

  // make ( y = con_i(..dec(y)..) -> smaller(dec(y)))  for each constructor 
  FormulaList* conjunction = new FormulaList(new AtomicFormula(Ly),0); 
  for(unsigned i=0;i<ta->nConstructors();i++){
    TermAlgebraConstructor* con = ta->constructor(i);
    unsigned arity = con->arity();

    // ignore a constructor if it doesn't mention ta_sort
    bool ignore = (arity == 0);
    if(!ignore){
      ignore=true;
      for(unsigned j=0;j<arity; j++){ 
        if(con->argSort(j)==ta_sort){
          ignore=false;
        } 
      } 
    }

    if(!ignore){
      // First generate all argTerms and remember those that are of sort ta_sort 
      Stack<TermList> argTerms;
      Stack<TermList> taTerms; 
      Stack<unsigned> ta_vars;
      Stack<TermList> varTerms;
      unsigned vars = 3;
      for(unsigned j=0;j<arity;j++){
        unsigned dj = con->destructorFunctor(j);
        TermList djy(Term::create1(dj,y));
        argTerms.push(djy);
        TermList xj(vars,false);
        varTerms.push(xj);
        if(con->argSort(j) == ta_sort){
          taTerms.push(djy);
          ta_vars.push(vars);
        }
        vars++;
      }
      // create y = con1(...d1(y)...d2(y)...)
      TermList coni(Term::create(con->functor(),(unsigned)argTerms.size(), argTerms.begin()));
      Literal* kneq = Literal::createEquality(true,y,coni,ta_sort);

      // create smaller(cons(x1,..,xn))
      Formula* smaller_coni = new AtomicFormula(Literal::create1(sty,true,
                                TermList(Term::create(con->functor(),(unsigned)varTerms.size(),varTerms.begin()))));

      FormulaList* smallers = 0;
      Stack<unsigned>::Iterator vtit(ta_vars);
      while(vtit.hasNext()){
        smallers = new FormulaList(new AtomicFormula(Literal::create1(sty,true,TermList(vtit.next(),false))),smallers);
      }
      Formula* ax = Formula::quantify(new BinaryFormula(Connective::IMP,smaller_coni, 
                     (FormulaList::length(smallers) > 1) ? new JunctionFormula(Connective::AND,smallers)
                                            : smallers->head()
                     ));

      // now create a conjunction of smaller(d(y)) for each d
      FormulaList* And = FormulaList::empty(); 
      Stack<TermList>::Iterator tit(taTerms);
      unsigned and_terms = 0;
      while(tit.hasNext()){
        Formula* f = new AtomicFormula(Literal::create1(sty,true,tit.next()));
        And = new FormulaList(f,And);
        and_terms++;
      }
      ASS(and_terms>0);
      Formula* imp = new BinaryFormula(Connective::IMP,
                            new AtomicFormula(kneq),
                            (and_terms>1) ? new JunctionFormula(Connective::AND,And)
                                          : And->head()
                            );
      

      conjunction = new FormulaList(imp,conjunction);
      conjunction = new FormulaList(ax,conjunction);
    } 
  }
  // now !z : smallerThanY(z) => ~L[z]
  TermReplacement cr2(term,z);
  Formula* smallerImpNL = Formula::quantify(new BinaryFormula(Connective::IMP, 
                            new AtomicFormula(Literal::create1(sty,true,z)),
                            new AtomicFormula(cr2.transform(clit))));

  conjunction = new FormulaList(smallerImpNL,conjunction);
  Formula* exists = new QuantifiedFormula(Connective::EXISTS, new Formula::VarList(y.var(),0),0,
                       new JunctionFormula(Connective::AND,conjunction));

  TermReplacement cr3(term,x);
  Literal* conclusion = cr3.transform(clit);
  FormulaList* orf = new FormulaList(exists,new FormulaList(Formula::quantify(new AtomicFormula(conclusion)),0));
  Formula* hypothesis = new JunctionFormula(Connective::OR,orf);

  //cout << hypothesis->toString() << endl;

  NewCNF cnf(0);
  cnf.setForInduction();
  Stack<Clause*> hyp_clauses;
  FormulaUnit* fu = new FormulaUnit(hypothesis,new Inference(Inference::INDUCTION),Unit::AXIOM);
  cnf.clausify(NNF::ennf(fu), hyp_clauses);

  //cout << "Clausify " << fu->toString() << endl;

  // Now perform resolution between lit and the hyp_clauses on clit, which should be contained in each clause!
  Stack<Clause*>::Iterator cit(hyp_clauses);
  while(cit.hasNext()){
    Clause* c = cit.next();
    static ResultSubstitutionSP identity = ResultSubstitutionSP(new IdentitySubstitution());
    SLQueryResult qr(lit,premise,identity);
    Clause* r = BinaryResolution::generateClause(c,conclusion,qr,*env.options);
    _clauses.push(r);
  }
  env.statistics->induction++; 
}

bool InductionClauseIterator::notDone(Literal* lit, Term* term)
{
  CALL("InductionClauseIterator::notDone");

  static DHSet<Literal*> done;
  static DHMap<unsigned,TermList> blanks; 
  unsigned srt = env.signature->getFunction(term->functor())->fnType()->result();

  if(!blanks.find(srt)){
    unsigned fresh = env.signature->addFreshFunction(0,"blank");
    env.signature->getFunction(fresh)->setType(OperatorType::getConstantsType(srt));
    TermList blank = TermList(Term::createConstant(fresh));
    blanks.insert(srt,blank);
  }

  TermReplacement cr(term,blanks.get(srt));
  Literal* rep = cr.transform(lit);

  if(done.contains(rep)){ 
    return false; 
  }

  done.insert(rep);

  return true;
}

}// namespace Inferences
