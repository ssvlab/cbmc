/*******************************************************************\

Module: Cover a set of goals incrementally

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include <util/threeval.h>

#include "literal_expr.h"
#include "cover_goals.h"

/*******************************************************************\

Function: cover_goalst::~cover_goalst

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

cover_goalst::~cover_goalst()
{
}

/*******************************************************************\

Function: cover_goalst::mark

  Inputs:

 Outputs:

 Purpose: Mark goals that are covered

\*******************************************************************/

void cover_goalst::mark()
{
  // notify observers
  for(const auto & o : observers)
    o->satisfying_assignment();

  for(auto & g : goals)
    if(g.status==goalt::statust::UNKNOWN &&
       prop_conv.l_get(g.condition).is_true())
    {
      g.status=goalt::statust::COVERED;
      _number_covered++;
      
      // notify observers
      for(const auto & o : observers)
        o->goal_covered(g);
    }
}
  
/*******************************************************************\

Function: cover_goalst::constaint

  Inputs:

 Outputs:

 Purpose: Build clause

\*******************************************************************/

void cover_goalst::constraint()
{
  exprt::operandst disjuncts;
  
  // cover at least one unknown goal

  for(std::list<goalt>::const_iterator
      g_it=goals.begin();
      g_it!=goals.end();
      g_it++)
    if(g_it->status==goalt::statust::UNKNOWN &&
       !g_it->condition.is_false())
      disjuncts.push_back(literal_exprt(g_it->condition));

  // this is 'false' if there are no disjuncts
  prop_conv.set_to_true(disjunction(disjuncts));
}

/*******************************************************************\

Function: cover_goalst::freeze_goal_variables

  Inputs:

 Outputs:

 Purpose: Build clause

\*******************************************************************/

void cover_goalst::freeze_goal_variables()
{
  for(std::list<goalt>::const_iterator
      g_it=goals.begin();
      g_it!=goals.end();
      g_it++)
    if(!g_it->condition.is_constant())
      prop_conv.set_frozen(g_it->condition);
}

/*******************************************************************\

Function: cover_goalst::operator()

  Inputs:

 Outputs:

 Purpose: Try to cover all goals

\*******************************************************************/

decision_proceduret::resultt cover_goalst::operator()()
{
  _iterations=_number_covered=0;
  
  decision_proceduret::resultt dec_result;
  
  // We use incremental solving, so need to freeze some variables
  // to prevent them from being eliminated.      
  freeze_goal_variables();

  do
  {
    // We want (at least) one of the remaining goals, please!
    _iterations++;
    
    constraint();
    dec_result=prop_conv.dec_solve();
    
    switch(dec_result)
    {
    case decision_proceduret::D_UNSATISFIABLE: // DONE
      return dec_result;

    case decision_proceduret::D_SATISFIABLE:
      // mark the goals we got, and notify observers
      mark(); 
      break;

    default:
      error() << "decision procedure has failed" << eom;
      return dec_result;
    }
  }
  while(dec_result==decision_proceduret::D_SATISFIABLE &&
        number_covered()<size());

  return decision_proceduret::D_SATISFIABLE;
}

