
/*
 * File FOOLElimination.cpp.
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
 * @file FOOLElimination.cpp
 * Implements the FOOL-to-FOL translation procedure, described in "A First
 * Class Boolean Sort in First-Order Theorem Proving and TPTP" [1] by
 * Kotelnikov, Kovacs and Voronkov.
 *
 * [1] http://arxiv.org/abs/1505.01682
 */

#include "Indexing/TermSharing.hpp"

#include "Lib/Environment.hpp"

#include "Kernel/Clause.hpp"
#include "Kernel/FormulaUnit.hpp"
#include "Kernel/Inference.hpp"
#include "Kernel/Problem.hpp"
#include "Kernel/Signature.hpp"
#include "Kernel/SortHelper.hpp"
#include "Kernel/SubformulaIterator.hpp"

#include "Shell/Options.hpp"
#include "Shell/SymbolOccurrenceReplacement.hpp"

#include "Rectify.hpp"

#include "FOOLElimination.hpp"

using namespace Lib;
using namespace Kernel;
using namespace Shell;

// Prefixes for fresh symbols
const char* FOOLElimination::ITE_PREFIX  = "iG";
const char* FOOLElimination::LET_PREFIX  = "lG";
const char* FOOLElimination::BOOL_PREFIX = "bG";

// The default input type of introduced definitions
const Unit::InputType FOOLElimination::DEFINITION_INPUT_TYPE = Unit::AXIOM;

FOOLElimination::FOOLElimination() : _defs(0) {}

bool FOOLElimination::needsElimination(FormulaUnit* unit) {
  CALL("FOOLElimination::needsElimination");

  /**
   * Be careful with the difference between FOOLElimination::needsElimination
   * and Property::_hasFOOL!
   *
   * The former checks that the formula has subterms that are not syntactically
   * first-order. That only includes boolean variables, used as formulas,
   * formulas, used as terms, $let and $ite. This check is needed to decide
   * whether any of the transformations from FOOLElimination are yet to be
   * applied.
   *
   * The latter checks if there is an occurrence of any boolean term. This is
   * needed to identify if boolean theory axioms must be added to the search
   * space.
   *
   * For an example, consider the following falsum:
   * tff(1, conjecture, ![X : $o, Y : $o]: (X = Y)).
   *
   * $o will be parsed as $bool, and the equality will be parsed simply as
   * equality between $bool. No transformations from FOOLElimination are needed
   * to be applied here, however, theory axioms are needed to be added. Thus,
   * FOOLElimination::needsElimination should return false for this input,
   * whereas Property::_hasFOOL must be set to true.
   */

  SubformulaIterator sfi(unit->formula());
  while(sfi.hasNext()) {
    Formula* formula = sfi.next();
    switch (formula->connective()) {
      case LITERAL:
        if (!formula->literal()->shared()) {
          return true;
        }
        break;
      case BOOL_TERM:
        return true;
      default:
        break;
    }
  }
  return false;
}

void FOOLElimination::apply(Problem& prb)  {
  CALL("FOOLElimination::apply(Problem*)");

  apply(prb.units());
  prb.reportFOOLEliminated();
  prb.invalidateProperty();
}

void FOOLElimination::apply(UnitList*& units) {
  CALL("FOOLElimination::apply(UnitList*&)");

  UnitList::DelIterator us(units);
  while(us.hasNext()) {
    Unit* unit = us.next();
    if(unit->isClause()) {
      Clause* clause = static_cast<Clause*>(unit);
      for (unsigned i = 0; i < clause->length(); i++) {
        // we do not allow special terms in clauses so we check that all clause literals
        // are shared (special terms can not be shared)
        if(!(*clause)[i]->shared()){ 
          USER_ERROR("Input clauses (cnf) cannot use $ite, $let or $o terms. Error in "+clause->literalsOnlyToString());
        }
      }
      continue;
    }
    Unit* processedUnit = apply(static_cast<FormulaUnit*>(unit));
    if (processedUnit != unit) {
      us.replace(processedUnit);
    }
  }

  // Note that the "$true != $false" axiom is treated as a theory axiom and
  // added in TheoryAxiom.cpp

  units = UnitList::concat(_defs, units);
  _defs = 0;
}

FormulaUnit* FOOLElimination::apply(FormulaUnit* unit) {
  CALL("FOOLElimination::apply(FormulaUnit*)");

  if (!needsElimination(unit)) {
    return unit;
  }

  FormulaUnit* rectifiedUnit = Rectify::rectify(unit);

  Formula* formula = rectifiedUnit->formula();

  _unit = rectifiedUnit;
  _varSorts.reset();

  SortHelper::collectVariableSorts(formula, _varSorts);

  Formula* processedFormula = process(formula);
  if (formula == processedFormula) {
    return rectifiedUnit;
  }

  Inference* inference = new Inference1(Inference::FOOL_ELIMINATION, rectifiedUnit);
  FormulaUnit* processedUnit = new FormulaUnit(processedFormula, inference, rectifiedUnit->inputType());

  if (unit->included()) {
    processedUnit->markIncluded();
  }

  if (env.options->showPreprocessing()) {
    env.beginOutput();
    env.out() << "[PP] " << unit->toString() << endl;
    env.out() << "[PP] " << processedUnit->toString()  << endl;
    env.endOutput();
  }

  return processedUnit;
}

Formula* FOOLElimination::process(Formula* formula) {
  CALL("FOOLElimination::process(Formula*)");

  switch (formula->connective()) {
    case LITERAL: {
      Literal* literal = formula->literal();

      /**
       * Processing of a literal simply propagates processing to its arguments,
       * except for a case when it is an equality and one of the arguments is a
       * formula-as-term. In that case we build an equivalence between both
       * arguments, processed as formulas.
       *
       * For example, assume a and b are formulas and X is a boolean variable.
       * Then, a = b will be translated to a' <=> b', where a' and b' are
       * processed a and b, respectively. a = X will be translated as
       * a' <=> (X = true).
       *
       * The semantics of FOOL does not distinguish between equality and equivalence
       * between boolean terms and this special case implements a more natural way of
       * expressing an equality between formulas in FOL. It is not, however, strictly
       * needed - without it the equality would be processed simply as equality
       * between FOOL boolean terms.
       */

      if (literal->isEquality()) {
        ASS_EQ(literal->arity(), 2); // can there be equality between several terms?
        TermList lhs = *literal->nthArgument(0);
        TermList rhs = *literal->nthArgument(1);

        bool lhsIsFormula = lhs.isTerm() && lhs.term()->isBoolean();
        bool rhsIsFormula = rhs.isTerm() && rhs.term()->isBoolean();

        if (rhsIsFormula || lhsIsFormula) {
          Formula* lhsFormula = processAsFormula(lhs);
          Formula* rhsFormula = processAsFormula(rhs);

          Connective connective = literal->polarity() ? IFF : XOR;
          Formula* processedFormula = new BinaryFormula(connective, lhsFormula, rhsFormula);

          if (env.options->showPreprocessing()) {
            reportProcessed(formula->toString(), processedFormula->toString());
          }

          return processedFormula;
        }
      }

      Stack<TermList> arguments;
      Term::Iterator lit(literal);
      while (lit.hasNext()) {
        arguments.push(process(lit.next()));
      }

      Formula* processedFormula = new AtomicFormula(Literal::create(literal, arguments.begin()));

      if (env.options->showPreprocessing()) {
        reportProcessed(formula->toString(), processedFormula->toString());
      }

      return processedFormula;
    }

    case IFF:
    case XOR: {
      /**
       * Processing of a binary formula simply propagates processing to its
       * arguments, except for a case when it is an equivalence between two
       * boolean terms. In that case we build an equality between processed
       * underlying boolean terms.
       *
       * The semantics of FOOL does not distinguish between equality and
       * equivalence between boolean terms and this special case implements
       * a more natural way of expressing an equality between formulas in FOL.
       * It is not, however, strictly needed - without it the equality would be
       * processed simply as equality between FOOL boolean terms.
       */
      Formula* lhs = formula->left();
      Formula* rhs = formula->right();
      if (lhs->connective() == BOOL_TERM && rhs->connective() == BOOL_TERM) {
        TermList lhsTerm = lhs->getBooleanTerm();
        TermList rhsTerm = rhs->getBooleanTerm();

        bool polarity = formula->connective() == IFF;

        Literal* equality = Literal::createEquality(polarity, process(lhsTerm), process(rhsTerm), Sorts::SRT_BOOL);
        Formula* processedFormula = new AtomicFormula(equality);

        if (env.options->showPreprocessing()) {
          reportProcessed(formula->toString(), processedFormula->toString());
        }

        return processedFormula;
      }
      // deliberately no break here so that we would jump to the IMP case
    }

    case IMP:
      return new BinaryFormula(formula->connective(), process(formula->left()), process(formula->right()));

    case AND:
    case OR:
      return new JunctionFormula(formula->connective(), process(formula->args()));

    case NOT:
      return new NegatedFormula(process(formula->uarg()));

    case FORALL:
    case EXISTS:
      return new QuantifiedFormula(formula->connective(), formula->vars(),formula->sorts(), process(formula->qarg()));

    case BOOL_TERM: {
      Formula* processedFormula = processAsFormula(formula->getBooleanTerm());

      if (env.options->showPreprocessing()) {
        reportProcessed(formula->toString(), processedFormula->toString());
      }

      return processedFormula;
    }

    case TRUE:
    case FALSE:
      return formula;

#if VDEBUG
    default:
      ASSERTION_VIOLATION;
#endif
  }
}

FormulaList* FOOLElimination::process(FormulaList* formulas) {
  CALL ("FOOLElimination::process(FormulaList*)");

  FormulaList*  res = FormulaList::empty();
  FormulaList** ipt = &res;

  while (!FormulaList::isEmpty(formulas)) {
    Formula* processed = process(formulas->head());
    *ipt = new FormulaList(processed,FormulaList::empty());
    ipt = (*ipt)->tailPtr();
    formulas = formulas->tail();
  }

  return res;

  // return FormulaList::isEmpty(formulas) ? formulas : new FormulaList(process(formulas->head()), process(formulas->tail()));
}

/**
 * Processes a list of terms.
 *
 * Takes a context argument (whos value is either TERM_CONTEXT or
 * FORMULA_CONTEXT) and rather than returning the result of processing, writes
 * it to termResult (when context is TERM_CONTEXT) or formulaResult (when
 * context is FORMULA_CONTEXT). In other words, the result of processing is
 * either a term or a formula, depending on the context.
 *
 * The meaning of the context is the following. Rather than having two versions
 * of e.g. $ite-expressions (term-level and formula-level), this implementation
 * considers only the term-level case, the formula case is encoded using the
 * formula-inside-term special case of term. That is, the formula $ite(C, A, B),
 * where A, B and C are all formulas, is stored as $formula{$ite(C, $term{A}, $term{B})}.
 * The processing of an $ite-term should be different, depending on whether or
 * not it occures directly under $formula. In the former case, we should unpack
 * A and B from $term and introduce a fresh predicate symbol, whereas in the
 * latter case we should introduce a fresh function symbol. So, the context
 * argument tells the process function if the term is inside of a $formula.
 *
 * A similar reasoning is applied to the way $let-terms are stored.
 *
 * An alternative and slightly simpler implementation would have been to always
 * go for the TERM_CONTEXT case. That way, instead of introducing a fresh
 * predicate symbol when inside $formula, we would introduce a fresh function
 * symbol of the sort SRT_BOOL. That would result, however, in more
 * definitions with more boilerplate. In particular, instead of predicate
 * applications we would have equalities of function symbol applications to
 * FOOL_TRUE all over the place.
 */
void FOOLElimination::process(TermList ts, Context context, TermList& termResult, Formula*& formulaResult) {
  CALL("FOOLElimination::process(TermList ts, Context context, ...)");

#if VDEBUG
  // A term can only be processed in a formula context if it has boolean sort
  // The opposite does not hold - a boolean term can stand in a term context
  unsigned sort = SortHelper::getResultSort(ts, _varSorts);
  if (context == FORMULA_CONTEXT) {
    ASS_REP(sort == Sorts::SRT_BOOL, ts.toString());
  }
#endif

  if (!ts.isTerm()) {
    if (context == TERM_CONTEXT) {
      termResult = ts;
    } else {
      formulaResult = toEquality(ts);
    }
    return;
  }

  process(ts.term(), context, termResult, formulaResult);

  if (context == FORMULA_CONTEXT) {
    return;
  }

  // preprocessing of the term does not affect the sort
  ASS_EQ(sort, SortHelper::getResultSort(termResult, _varSorts));
}

/**
 * A shortcut of process(TermList, context) for TERM_CONTEXT.
 */
TermList FOOLElimination::process(TermList terms) {
  CALL("FOOLElimination::process(TermList terms)");
  TermList ts;
  Formula* dummy;
  process(terms, TERM_CONTEXT, ts, dummy);
  return ts;
}

/**
 * A shortcut of process(TermList, context) for FORMULA_CONTEXT.
 */
Formula* FOOLElimination::processAsFormula(TermList terms) {
  CALL("FOOLElimination::processAsFormula(TermList terms)");
  Formula* formula;
  TermList dummy;
  process(terms, FORMULA_CONTEXT, dummy, formula);
  return formula;
}

/**
 * Process a given term. The actual work is done if the term is special.
 *
 * Similarly to process(TermList, context, ...) takes a context as an argument
 * and depending on its value (TERM_CONTEXT or FORMULA_CONTEXT) writes the
 * result to termResult or formulaResult, respectively.
 *
 * Returns TermList rather than Term* to cover the situation when $let-term
 * with a variable body is processed. That way, the result would be just the
 * variable, and we cannot put it inside Term*.
 *
 * Note that process() is called recursively on all the subterms of the given
 * term. That way, every definition that is put into _defs doesn't have FOOL
 * subterms and we don't have to further process it.
 */
void FOOLElimination::process(Term* term, Context context, TermList& termResult, Formula*& formulaResult) {
  CALL("FOOLElimination::process(Term* term, Context context, ...)");

  // collect free variables of the term and their sorts
  Formula::VarList* freeVars = term->freeVariables();
  Stack<unsigned> freeVarsSorts = collectSorts(freeVars);

  /**
   * Note that we collected free variables before processing subterms. That
   * assumes that process() preserves free variables. This assumption relies
   * on the fact that $ite and formula terms are rewritten into an fresh symbol
   * applied to free variables, and the processing of $let-terms itself doesn't
   * remove occurrences of variables. An assertion at the end of this method
   * checks that free variables of the input and the result coincide.
   */

  if (!term->isSpecial()) {
    /**
     * If term is not special, simply propagate processing to its arguments.
     */

    Stack<TermList> arguments;
    Term::Iterator ait(term);
    while (ait.hasNext()) {
      arguments.push(process(ait.next()));
    }

    TermList processedTerm = TermList(Term::create(term, arguments.begin()));

    if (context == FORMULA_CONTEXT) {
      formulaResult = toEquality(processedTerm);
    } else {
      termResult = processedTerm;
    }
  } else {
    /**
     * In order to process a special term (that is, a term that is syntactically
     * valid in FOOL but not in FOL), we will replace the whole term or some of
     * its parts with an application of a fresh function symbol and add one or
     * more definitions of this symbol to _defs.
     *
     * To prevent variables from escaping their lexical scope, we collect those
     * of them, that have free occurrences in the term and make the applications
     * of the fresh symbol take them as arguments.
     *
     * Note that we don't have to treat in a similar way occurrences of function
     * symbols, defined in $let-expressions, because the parser already made sure
     * to resolve scope issues, made them global (by renaming) and added them to
     * the signature. The only thing to be cautious about is that processing of
     * the contents of the $let-term should be done after the occurrences of the
     * defined symbol in it are replaced with the fresh one.
     */

    Term::SpecialTermData* sd = term->getSpecialData();

    switch (term->functor()) {
      case Term::SF_ITE: {
        /**
         * Having a term of the form $ite(f, s, t) and the list X1, ..., Xn of
         * its free variables (it is the union of free variables of f, s and t)
         * we will do the following:
         *  1) Create a fresh function symbol g of arity n that spans over sorts
         *     of X1, ..., Xn and the return sort of the term
         *  2) Add two definitions:
         *     * ![X1, ..., Xn]: ( f => g(X1, ..., Xn) = s)
         *     * ![X1, ..., Xn]: (~f => g(X1, ..., Xn) = t)
         *  3) Replace the term with g(X1, ..., Xn)
         */

        Formula* condition = process(sd->getCondition());

        TermList thenBranch;
        Formula* thenBranchFormula;
        process(*term->nthArgument(0), context, thenBranch, thenBranchFormula);

        TermList elseBranch;
        Formula* elseBranchFormula;
        process(*term->nthArgument(1), context, elseBranch, elseBranchFormula);

        // the sort of the term is the sort of the then branch
        unsigned resultSort = 0;
        if (context == TERM_CONTEXT) {
          resultSort = SortHelper::getResultSort(thenBranch, _varSorts);
          ASS_EQ(resultSort, SortHelper::getResultSort(elseBranch, _varSorts));
        }

        // create a fresh symbol g
        unsigned freshSymbol = introduceFreshSymbol(context, ITE_PREFIX, freeVarsSorts, resultSort);

        // build g(X1, ..., Xn)
        TermList freshFunctionApplication;
        Formula* freshPredicateApplication;
        buildApplication(freshSymbol, freeVars, context, freshFunctionApplication, freshPredicateApplication);

        // build g(X1, ..., Xn) == s
        Formula* thenEq = buildEq(context, freshPredicateApplication, thenBranchFormula,
                                           freshFunctionApplication, thenBranch, resultSort);

        // build (f => g(X1, ..., Xn) == s)
        Formula* thenImplication = new BinaryFormula(IMP, condition, thenEq);

        // build ![X1, ..., Xn]: (f => g(X1, ..., Xn) == s)
        if (Formula::VarList::length(freeVars) > 0) {
          //TODO do we know the sorts of freeVars?
          thenImplication = new QuantifiedFormula(FORALL, freeVars,0, thenImplication);
        }

        // build g(X1, ..., Xn) == t
        Formula* elseEq = buildEq(context, freshPredicateApplication, elseBranchFormula,
                                           freshFunctionApplication, elseBranch, resultSort);

        // build ~f => g(X1, ..., Xn) == t
        Formula* elseImplication = new BinaryFormula(IMP, new NegatedFormula(condition), elseEq);

        // build ![X1, ..., Xn]: (~f => g(X1, ..., Xn) == t)
        if (Formula::VarList::length(freeVars) > 0) {
          //TODO do we know the sorts of freeVars?
          elseImplication = new QuantifiedFormula(FORALL, freeVars, 0, elseImplication);
        }

        // add both definitions
        Inference* iteInference = new Inference1(Inference::FOOL_ITE_ELIMINATION, _unit);
        addDefinition(new FormulaUnit(thenImplication, iteInference, DEFINITION_INPUT_TYPE));
        addDefinition(new FormulaUnit(elseImplication, iteInference, DEFINITION_INPUT_TYPE));

        if (context == FORMULA_CONTEXT) {
          formulaResult = freshPredicateApplication;
        } else {
          termResult = freshFunctionApplication;
        }
        break;
      }

      case Term::SF_LET: {
        /**
         * Having a term of the form $let(f(Y1, ..., Yk) := s, t), where f is a
         * function or predicate symbol and the list X1, ..., Xn of free variables
         * of the binding of f (it is the set of free variables of s minus
         * Y1, ..., Yk) we will do the following:
         *  1) Create a fresh function or predicate symbol g (depending on which
         *     one is f) of arity n + k that spans over sorts of
         *     X1, ..., Xn, Y1, ..., Yk
         *  2) If f is a predicate symbol, add the following definition:
         *       ![X1, ..., Xn, Y1, ..., Yk]: g(X1, ..., Xn, Y1, ..., Yk) <=> s
         *     Otherwise, add
         *       ![X1, ..., Xn, Y1, ..., Yk]: g(X1, ..., Xn, Y1, ..., Yk) = s
         *  3) Build a term t' by replacing all of its subterms of the form
         *     f(t1, ..., tk) by g(X1, ..., Xn, t1, ..., tk)
         *  4) Replace the term with t'
         */

        TermList binding = sd->getBinding(); // deliberately unprocessed here

        /**
         * $let-expressions are used for binding both function and predicate symbols.
         * The body of the binding, however, is always stored as a term. When f is a
         * predicate, the body is a formula, wrapped in a term. So, this is how we
         * check that it is a predicate binding and the body of the function stands in
         * the formula context
         */
        Context bindingContext = binding.isTerm() && binding.term()->isBoolean() ? FORMULA_CONTEXT : TERM_CONTEXT;

        // collect variables Y1, ..., Yk
        Formula::VarList* argumentVars = sd->getVariables();

        // collect variables X1, ..., Xn
        Formula::VarList* bodyFreeVars(0);
        Formula::VarList::Iterator bfvi(binding.freeVariables());
        while (bfvi.hasNext()) {
          int var = bfvi.next();
          if (!Formula::VarList::member(var, argumentVars)) {
            bodyFreeVars = new Formula::VarList(var, bodyFreeVars);
          }
        }

        // build the list X1, ..., Xn, Y1, ..., Yk of variables and their sorts
        Formula::VarList* vars = Formula::VarList::append(bodyFreeVars, argumentVars);
        Stack<unsigned> sorts = collectSorts(vars);

        // take the defined function symbol and its result sort
        unsigned symbol = sd->getFunctor();
        unsigned bindingSort = SortHelper::getResultSort(binding, _varSorts);

        /**
         * Here we can take a simple shortcut. If the there are no free variables,
         * f and g would have the same type, but g would have an ugly generated name.
         * So, in this case, instead of creating a new symbol, we will just
         * reuse f and leave the t term as it is.
         */
        bool renameSymbol = Formula::VarList::isNonEmpty(bodyFreeVars);
        
        /**
         * If the symbol is not marked as introduced then this means it was used
         * in the input after introduction, therefore it should be renamed here
         */
        if(bindingContext == TERM_CONTEXT && !env.signature->getFunction(symbol)->introduced()) renameSymbol = true;
        if(bindingContext == FORMULA_CONTEXT && !env.signature->getPredicate(symbol)->introduced()) renameSymbol = true;

        // create a fresh function or predicate symbol g
        unsigned freshSymbol = renameSymbol ? introduceFreshSymbol(bindingContext, LET_PREFIX, sorts, bindingSort) : symbol;

        // process the body of the function
        TermList processedBody;
        Formula* processedBodyFormula;
        process(binding, bindingContext, processedBody, processedBodyFormula);

        // build g(X1, ..., Xn, Y1, ..., Yk)
        TermList freshFunctionApplication;
        Formula* freshPredicateApplication;
        buildApplication(freshSymbol, vars, bindingContext, freshFunctionApplication, freshPredicateApplication);

        // build g(X1, ..., Xn, Y1, ..., Yk) == s
        Formula* freshSymbolDefinition = buildEq(bindingContext, freshPredicateApplication, processedBodyFormula,
                                                                 freshFunctionApplication, processedBody, bindingSort);

        // build ![X1, ..., Xn, Y1, ..., Yk]: g(X1, ..., Xn, Y1, ..., Yk) == s
        if (Formula::VarList::length(vars) > 0) {
        /*
          Formula::SortList* sortList = Formula::SortList::empty();
          Formula::VarList* vp = vars;
          while(vp){
            cout << vp->head() << endl;
            sortList = new Formula::SortList(sorts[vp->head()],sortList);
            vp = vp->tail();
            cout << vp << endl;
          }
        */ 
          freshSymbolDefinition = new QuantifiedFormula(FORALL, vars, 0, freshSymbolDefinition);
        }

        // add the introduced definition
        Inference* letInference = new Inference1(Inference::FOOL_LET_ELIMINATION, _unit);
        addDefinition(new FormulaUnit(freshSymbolDefinition, letInference, DEFINITION_INPUT_TYPE));

        TermList contents = *term->nthArgument(0); // deliberately unprocessed here

        // replace occurrences of f(t1, ..., tk) by g(X1, ..., Xn, t1, ..., tk)
        if (renameSymbol) {
          if (env.options->showPreprocessing()) {
            env.beginOutput();
            env.out() << "[PP] FOOL replace in:  " << contents.toString() << endl;
            env.endOutput();
          }

          SymbolOccurrenceReplacement replacement(bindingContext == FORMULA_CONTEXT, symbol, freshSymbol, bodyFreeVars);
          contents = replacement.process(contents);

          if (env.options->showPreprocessing()) {
            env.beginOutput();
            env.out() << "[PP] FOOL replace out: " << contents.toString() << endl;
            env.endOutput();
          }
        }

        process(contents, context, termResult, formulaResult);

        break;
      }

      case Term::SF_FORMULA: {
        if (context == FORMULA_CONTEXT) {
          formulaResult = process(sd->getFormula());
          break;
        }

        Connective connective = sd->getFormula()->connective();

        if (connective == TRUE) {
          termResult = TermList(Term::foolTrue());
          break;
        }

        if (connective == FALSE) {
          termResult = TermList(Term::foolFalse());
          break;
        }

        /**
         * Having a formula in a term context and the list X1, ..., Xn of its
         * free variables we will do the following:
         *  1) Create a fresh function symbol g of arity n that spans over sorts of X1, ..., Xn
         *  2) Add the definition: ![X1, ..., Xn]: (f <=> g(X1, ..., Xn) = true),
         *     where true is FOOL constant
         *  3) Replace the term with g(X1, ..., Xn)
         */

        Formula *formula = process(sd->getFormula());

        // create a fresh symbol g and build g(X1, ..., Xn)
        unsigned freshSymbol = introduceFreshSymbol(context, BOOL_PREFIX, freeVarsSorts, Sorts::SRT_BOOL);
        TermList freshSymbolApplication = buildFunctionApplication(freshSymbol, freeVars);

        // build f <=> g(X1, ..., Xn) = true
        Formula* freshSymbolDefinition = new BinaryFormula(IFF, formula, toEquality(freshSymbolApplication));

        // build ![X1, ..., Xn]: (f <=> g(X1, ..., Xn) = true)
        if (Formula::VarList::length(freeVars) > 0) {
          // TODO do we know the sorts of freeVars?
          freshSymbolDefinition = new QuantifiedFormula(FORALL, freeVars,0, freshSymbolDefinition);
        }

        // add the introduced definition
        Inference* inference = new Inference1(Inference::FOOL_ELIMINATION, _unit);
        addDefinition(new FormulaUnit(freshSymbolDefinition, inference, DEFINITION_INPUT_TYPE));

        termResult = freshSymbolApplication;
        break;
      }

#if VDEBUG
      default:
        ASSERTION_VIOLATION;
#endif
    }

    if (env.options->showPreprocessing()) {
      reportProcessed(term->toString(), context == FORMULA_CONTEXT ? formulaResult->toString() : termResult.toString());
    }
  }

#if VDEBUG
  // free variables of the input and the result should coincide
  Formula::VarList* resultFreeVars;
  if (context == TERM_CONTEXT) {
    resultFreeVars = termResult.isVar() ? new List<int>(termResult.var()) : termResult.term()->freeVariables();
  } else {
    resultFreeVars = formulaResult->freeVariables();
  }

  Formula::VarList::Iterator ufv(freeVars);
  while (ufv.hasNext()) {
    unsigned var = (unsigned)ufv.next();
    ASS_REP(Formula::VarList::member(var, resultFreeVars), var);
  }
  Formula::VarList::Iterator pfv(resultFreeVars);
  while (pfv.hasNext()) {
    unsigned var = (unsigned)pfv.next();
    ASS_REP(Formula::VarList::member(var, freeVars), var);
  }

  // special subterms should be eliminated
  if (context == TERM_CONTEXT) {
    ASS_REP(termResult.isSafe(), termResult);
  }
#endif
}

/**
 * A shortcut of process(Term*, context) for TERM_CONTEXT.
 */
TermList FOOLElimination::process(Term* term) {
  CALL("FOOLElimination::process(Term* term)");

  TermList termList;
  Formula* dummy;

  process(term, TERM_CONTEXT, termList, dummy);

  return termList;
}

/**
 * A shortcut of process(Term*, context) for FORMULA_CONTEXT.
 */
Formula* FOOLElimination::processAsFormula(Term* term) {
  CALL("FOOLElimination::processAsFormula(Term* term)");

  Formula* formula;
  TermList dummy;

  process(term, FORMULA_CONTEXT, dummy, formula);

  return formula;
}

/**
 * Given a symbol g of a given arity n and a list of variables X1, ..., Xn
 * builds a term g(X1, ..., Xn). Depending on a context, g is assumed to be
 * a function or a predicate symbol. In the former case, the result in written
 * to functionApplication, otherwise to predicateApplication.
 */
void FOOLElimination::buildApplication(unsigned symbol, Formula::VarList* vars, Context context,
                                       TermList& functionApplication, Formula*& predicateApplication) {
  CALL("FOOLElimination::buildApplication");

  unsigned arity = Formula::VarList::length(vars);
  if (context == FORMULA_CONTEXT) {
    ASS_EQ(env.signature->predicateArity(symbol), arity);
  } else {
    ASS_EQ(env.signature->functionArity(symbol), arity);
  }

  Stack<TermList> arguments;
  Formula::VarList::Iterator vit(vars);
  while (vit.hasNext()) {
    unsigned var = (unsigned)vit.next();
    arguments.push(TermList(var, false));
  }

  if (context == FORMULA_CONTEXT) {
    predicateApplication = new AtomicFormula(Literal::create(symbol, arity, true, false, arguments.begin()));
  } else {
    functionApplication = TermList(Term::create(symbol, arity, arguments.begin()));
  }
}

/**
 * A shortcut of buildApplication for TERM_CONTEXT.
 */
TermList FOOLElimination::buildFunctionApplication(unsigned function, Formula::VarList* vars) {
  CALL("FOOLElimination::buildFunctionApplication");

  TermList functionApplication;
  Formula* dummy;

  buildApplication(function, vars, TERM_CONTEXT, functionApplication, dummy);

  return functionApplication;
}

/**
 * A shortcut of buildApplication for FORMULA_CONTEXT.
 */
Formula* FOOLElimination::buildPredicateApplication(unsigned predicate, Formula::VarList* vars) {
  CALL("FOOLElimination::buildPredicateApplication");

  TermList dummy;
  Formula* predicateApplication;

  buildApplication(predicate, vars, FORMULA_CONTEXT, dummy, predicateApplication);

  return predicateApplication;
}

/**
 * Builds an equivalence or an equality between provided pairs of expressions.
 * The context argument guides which pair is takes and which of the two eq's is built.
 */
Formula* FOOLElimination::buildEq(Context context, Formula* lhsFormula, Formula* rhsFormula,
                                                   TermList lhsTerm, TermList rhsTerm, unsigned termSort) {
  CALL("FOOLElimination::buildEq");

  if (context == FORMULA_CONTEXT) {
    // build equivalence
    return new BinaryFormula(IFF, lhsFormula, rhsFormula);
  } else {
    // build equality
    return new AtomicFormula(Literal::createEquality(true, lhsTerm, rhsTerm, termSort));
  }
}

/**
 * Creates a stack of sorts for the given variables, using the sorting context
 * of the current formula.
 */
Stack<unsigned> FOOLElimination::collectSorts(Formula::VarList* vars) {
  CALL("FOOLElimination::collectSorts");

  Stack<unsigned> sorts;
  Formula::VarList::Iterator fvi(vars);
  while (fvi.hasNext()) {
    unsigned var = (unsigned)fvi.next();
    ASS_REP(_varSorts.find(var), var);
    sorts.push(_varSorts.get(var));
  }
  return sorts;
}

/**
 * Extends the list of definitions with a given unit, making sure that it
 * doesn't have FOOL subterms.
 */
void FOOLElimination::addDefinition(FormulaUnit* def) {
  CALL("FOOLElimination::addDefinition");

  ASS_REP(!needsElimination(def), def->toString());

  _defs = new UnitList(def, _defs);

  if (env.options->showPreprocessing()) {
    env.beginOutput();
    env.out() << "[PP] FOOL added definition: " << def->toString() << endl;
    env.endOutput();
  }
}

Formula* FOOLElimination::toEquality(TermList booleanTerm) {
  TermList truth(Term::foolTrue());
  Literal* equality = Literal::createEquality(true, booleanTerm, truth, Sorts::SRT_BOOL);
  return new AtomicFormula(equality);
}

unsigned FOOLElimination::introduceFreshSymbol(Context context, const char* prefix,
                                               Stack<unsigned> sorts, unsigned resultSort) {
  CALL("FOOLElimination::introduceFreshSymbol");

  unsigned arity = (unsigned)sorts.size();
  OperatorType* type;
  if (context == FORMULA_CONTEXT) {
    type = OperatorType::getPredicateType(arity, sorts.begin());
  } else {
    type = OperatorType::getFunctionType(arity, sorts.begin(), resultSort);
  }

  unsigned symbol;
  if (context == FORMULA_CONTEXT) {
    symbol = env.signature->addFreshPredicate(arity, prefix);
    env.signature->getPredicate(symbol)->setType(type);
  } else {
    symbol = env.signature->addFreshFunction(arity, prefix);
    env.signature->getFunction(symbol)->setType(type);
  }

  if (env.options->showPreprocessing()) {
    env.beginOutput();
    env.out() << "[PP] FOOL: introduced fresh ";
    if (context == FORMULA_CONTEXT) {
      env.out() << "predicate symbol " << env.signature->predicateName(symbol);
    } else {
      env.out() << "function symbol " << env.signature->functionName(symbol);
    }
    env.out() << " of the sort " << type->toString() << endl;
    env.endOutput();
  }

  return symbol;
}

void FOOLElimination::reportProcessed(vstring inputRepr, vstring outputRepr) {
  CALL("FOOLElimination::reportProcessed");

  if (inputRepr != outputRepr) {
    /**
     * If show_fool is set to off, the string representations of the input
     * and the output of process() may in some cases coincide, despite the
     * input and the output being different. Example: $term{$true} and
     * $true. In order to avoid misleading log messages with the input and
     * the output seeming the same, we will not log such processings at
     * all. Setting show_fool to on, however, will display everything.
     */
    env.beginOutput();
    env.out() << "[PP] FOOL in:  " << inputRepr  << endl;
    env.out() << "[PP] FOOL out: " << outputRepr << endl;
    env.endOutput();
  }
}
