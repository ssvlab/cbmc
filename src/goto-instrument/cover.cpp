/*******************************************************************\

Module: Coverage Instrumentation

Author: Daniel Kroening

Date: May 2016

\*******************************************************************/

#include <algorithm>
#include <iterator>

#include <util/prefix.h>
#include <util/i2string.h>
#include <util/expr_util.h>

#include "cover.h"

class basic_blockst
{
public:
  explicit basic_blockst(const goto_programt &_goto_program)
  {
    bool next_is_target=true;
    unsigned block_count=0;

    forall_goto_program_instructions(it, _goto_program)
    {
      if(next_is_target || it->is_target())
        block_count++;
        
      block_map[it]=block_count;
      
      if(!it->source_location.is_nil() &&
         source_location_map.find(block_count)==source_location_map.end())
        source_location_map[block_count]=it->source_location;
      
      next_is_target=
        it->is_goto() || it->is_function_call() || it->is_assume();
    }
  }

  // map program locations to block numbers  
  typedef std::map<goto_programt::const_targett, unsigned> block_mapt;
  block_mapt block_map;
  
  // map block numbers to source code locations
  typedef std::map<unsigned, source_locationt> source_location_mapt;
  source_location_mapt source_location_map;
  
  inline unsigned operator[](goto_programt::const_targett t)
  {
    return block_map[t];
  }
  
  void output(std::ostream &out)
  {
    for(block_mapt::const_iterator
        b_it=block_map.begin();
        b_it!=block_map.end();
        b_it++)
      out << b_it->first->source_location
          << " -> " << b_it->second
          << '\n';
  }
};

/*******************************************************************\

Function: as_string

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

const char *as_string(coverage_criteriont c)
{
  switch(c)
  {
  case coverage_criteriont::LOCATION: return "location";
  case coverage_criteriont::BRANCH: return "branch";
  case coverage_criteriont::DECISION: return "decision";
  case coverage_criteriont::CONDITION: return "condition";
  case coverage_criteriont::PATH: return "path";
  case coverage_criteriont::MCDC: return "MC/DC";
  case coverage_criteriont::ASSERTION: return "assertion";
  case coverage_criteriont::COVER: return "cover instructions";
  default: return "";
  }
}

/*******************************************************************\

Function: is_condition

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool is_condition(const exprt &src)
{
  if(src.type().id()!=ID_bool) return false;

  // conditions are 'atomic predicates'
  if(src.id()==ID_and || src.id()==ID_or ||
     src.id()==ID_not || src.id()==ID_implies)
    return false;
  
  return true;
}

/*******************************************************************\

Function: collect_conditions_rec

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void collect_conditions_rec(const exprt &src, std::set<exprt> &dest)
{
  if(src.id()==ID_address_of)
  {
    return;
  }

  for(const auto & op : src.operands())
    collect_conditions_rec(op, dest);

  if(is_condition(src) && !src.is_constant())
    dest.insert(src); 
}

/*******************************************************************\

Function: collect_conditions

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

std::set<exprt> collect_conditions(const exprt &src)
{
  std::set<exprt> result;
  collect_conditions_rec(src, result);
  return result;
}

/*******************************************************************\

Function: collect_conditions

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

std::set<exprt> collect_conditions(const goto_programt::const_targett t)
{
  switch(t->type)
  {
  case GOTO:
  case ASSERT:
    return collect_conditions(t->guard);
  
  case ASSIGN:
  case FUNCTION_CALL:
    return collect_conditions(t->code);
    
  default:;
  }
  
  return std::set<exprt>();
}

/*******************************************************************\

Function: collect_operands

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void collect_operands(const exprt &src, std::vector<exprt> &dest)
{
  for(const exprt &op : src.operands())
  {
    if(op.id()==src.id())
      collect_operands(op, dest);
    else
      dest.push_back(op);
  }
}

/*******************************************************************\

Function: collect_mcdc_controlling_rec

  Inputs:

 Outputs:

 Purpose: To recursively collect controlling exprs for
          for mcdc coverage.

\*******************************************************************/

void collect_mcdc_controlling_rec(
  const exprt &src,
  const std::vector<exprt> &conditions,
  std::set<exprt> &result)
{
  // src is conjunction (ID_and) or disjunction (ID_or)
  if(src.id()==ID_and ||
     src.id()==ID_or)
  {
    std::vector<exprt> operands;
    collect_operands(src, operands);

    if(operands.size()==1)
    {
      exprt e=*operands.begin();
      collect_mcdc_controlling_rec(e, conditions, result);
    }
    else if(!operands.empty())
    {
      for(std::size_t i=0; i<operands.size(); i++)
      {
        const exprt op=operands[i];
      
        if(is_condition(op))
        {
          if(src.id()==ID_or)
          {
            std::vector<exprt> others1, others2;
            if(!conditions.empty())
            {
              others1.push_back(conjunction(conditions));
              others2.push_back(conjunction(conditions));
            }
            
            for(std::size_t j=0; j<operands.size(); j++)
            {
              others1.push_back(not_exprt(operands[j]));
              if(i!=j)
                others2.push_back(not_exprt(operands[j]));
              else
                others2.push_back((operands[j]));
            }

            result.insert(conjunction(others1));
            result.insert(conjunction(others2));
            continue;
          }

          std::vector<exprt> o=operands;
        
          // 'o[i]' needs to be true and false
          std::vector<exprt> new_conditions=conditions;
          new_conditions.push_back(conjunction(o));
          result.insert(conjunction(new_conditions));

          o[i].make_not();
          new_conditions.back()=conjunction(o);
          result.insert(conjunction(new_conditions));
        }
        else
        {
          std::vector<exprt> others;
          others.reserve(operands.size()-1);

          for(std::size_t j=0; j<operands.size(); j++)
            if(i!=j)
            {
              if(src.id()==ID_or)
                others.push_back(not_exprt(operands[j]));
              else
                others.push_back(operands[j]);
            }
            
          exprt c=conjunction(others);
          std::vector<exprt> new_conditions=conditions;
          new_conditions.push_back(c);

          collect_mcdc_controlling_rec(op, new_conditions, result);
        }
      }
    }
  }
  else if(src.id()==ID_not)
  {
    exprt e=to_not_expr(src).op();
    if(!is_condition(e))
      collect_mcdc_controlling_rec(e, conditions, result);
    else
    {
      // to store a copy of ''src''
      std::vector<exprt> new_conditions1=conditions;
      new_conditions1.push_back(src);
      result.insert(conjunction(new_conditions1));

      // to store a copy of its negation, i.e., ''e''
      std::vector<exprt> new_conditions2=conditions;
      new_conditions2.push_back(e);
      result.insert(conjunction(new_conditions2));
    }
  }
  else
  {
    /**
     * It may happen that ''is_condition(src)'' is valid,
     * but we ignore this case here as it can be handled
     * by the routine decision/condition detection.
     **/
  }
}

/*******************************************************************\

Function: collect_mcdc_controlling

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

std::set<exprt> collect_mcdc_controlling(
  const std::set<exprt> &decisions)
{
  std::set<exprt> result;
  
  for(const auto &d : decisions)
    collect_mcdc_controlling_rec(d, { }, result);

  return result;
}

/*******************************************************************\

Function: replacement_conjunction

  Inputs:

 Outputs:

 Purpose: To replace the i-th expr of ''operands'' with each
          expr inside ''replacement_exprs''.

\*******************************************************************/

std::set<exprt> replacement_conjunction(
  const std::set<exprt> &replacement_exprs,
  const std::vector<exprt> &operands,
  const std::size_t i)
{
  std::set<exprt> result;
  for(auto &y : replacement_exprs)
  {
    std::vector<exprt> others;
    for(std::size_t j=0; j<operands.size(); j++)
      if(i!=j)
        others.push_back(operands[j]);

    others.push_back(y);
    exprt c=conjunction(others);
    result.insert(c);
  }
  return result;
}
        
/*******************************************************************\

Function: collect_mcdc_controlling_nested

  Inputs:

 Outputs:

 Purpose: This nested method iteratively applies 
          ''collect_mcdc_controlling'' to every non-atomic expr
          within a decision

\*******************************************************************/

std::set<exprt> collect_mcdc_controlling_nested(
  const std::set<exprt> &decisions)
{
  // To obtain the 1st-level controlling conditions
  std::set<exprt> controlling = collect_mcdc_controlling(decisions);

  std::set<exprt> result;
  // For each controlling condition, to check if it contains
  // non-atomic exprs
  for(auto &src : controlling)
  {
    std::set<exprt> s1, s2;
    /**
     * The final controlling conditions resulted from ''src''
     * will be stored in ''s1''; ''s2'' is usd to hold the
     * temporary expansion.
     **/
    s1.insert(src);
    
    // dual-loop structure to eliminate complex 
    // non-atomic-conditional terms
    while(true) 
    {
      bool changed=false;
      // the 2nd loop
      for(auto &x : s1)
      {
        // if ''x'' is atomic conditional term, there
        // is no expansion
        if(is_condition(x))
        {
          s2.insert(x);
          continue;
        }
        // otherwise, we apply the ''nested'' method to
        // each of its operands
        std::vector<exprt> operands;
        collect_operands(x, operands);

        for(std::size_t i=0; i<operands.size(); i++)
        {
          std::set<exprt> res;
          /**
           * To expand an operand if it is not atomic,
           * and label the ''changed'' flag; the resulted 
           * expansion of such an operand is stored in ''res''.
           **/
          if(operands[i].id()==ID_not)
          {
            exprt no=operands[i].op0();
            if(!is_condition(no))
            {
              changed=true;
              std::set<exprt> ctrl_no;
              ctrl_no.insert(no);
              res=collect_mcdc_controlling(ctrl_no);
            }
          }
          else if(!is_condition(operands[i]))
          {
            changed=true;
            std::set<exprt> ctrl;
            ctrl.insert(operands[i]);
            res=collect_mcdc_controlling(ctrl);
          }

          // To replace a non-atomic expr with its expansion
          std::set<exprt> co=
            replacement_conjunction(res, operands, i);
          s2.insert(co.begin(), co.end());
          if(res.size() > 0) break;
        }
        // if there is no change x.r.t current operands of ''x'',
        // i.e., they are all atomic, we reserve ''x''
        if(!changed) s2.insert(x);
      }
      // update ''s1'' and check if change happens
      s1=s2;
      if(!changed) {break;}
      s2.clear();
    }

    // The expansions of each ''src'' should be added into
    // the ''result''
    result.insert(s1.begin(), s1.end());
  }
  
  return result;
}

/*******************************************************************\

Function: sign_of_expr

  Inputs: E should be the pre-processed output by
          ''collect_mcdc_controlling_nested''

 Outputs: +1 : not negated
          -1 : negated

 Purpose: The sign of expr ''e'' within the super-expr ''E''

\*******************************************************************/

std::set<signed> sign_of_expr(const exprt &e, const exprt &E)
{
  std::set<signed> signs;

  // At fist, we pre-screen the case such that ''E'' 
  // is an atomic condition
  if(is_condition(E))
  {
    if(e==E)
      signs.insert(+1);
    return signs;
  }

  // or, ''E'' is the negation of ''e''?
  if(E.id()==ID_not)
  {
    if(e==E.op0())
    {
      signs.insert(-1);
      return signs;
    }
  }

  /**
   * In the general case, we analyze each operand of ''E''.
   **/
  std::vector<exprt> ops;
  collect_operands(E, ops);
  for(auto &x : ops)
  {
    exprt y(x);
    if(y == e) signs.insert(+1);
    else if(y.id()==ID_not)
    {
      y.make_not();
      if(y==e) signs.insert(-1);
      if(!is_condition(y))
      {
        std::set<signed> re=sign_of_expr(e, y);
        signs.insert(re.begin(), re.end());
      }
    }
    else if(!is_condition(y))
    {
      std::set<signed> re=sign_of_expr(e, y);
      signs.insert(re.begin(), re.end());
    }
  }

  return signs;
}

/*******************************************************************\

Function: remove_repetition

  Inputs:

 Outputs:

 Purpose: After the ''collect_mcdc_controlling_nested'', there
          can be the recurrence of the same expr in the resulted
          set of exprs, and this method will remove the
          repetitive ones.

\*******************************************************************/

void remove_repetition(std::set<exprt> &exprs)
{
  // to obtain the set of atomic conditions
  std::set<exprt> conditions;
  for(auto &x: exprs)
  {
    std::set<exprt> new_conditions = collect_conditions(x);
    conditions.insert(new_conditions.begin(), new_conditions.end());
  }
  // exprt that contains multiple (inconsistent) signs should
  // be removed
  std::set<exprt> new_exprs;
  for(auto &x : exprs)
  {
    bool kept=true;
    for(auto &c : conditions)
    {
      std::set<signed> signs=sign_of_expr(c, x);
      if(signs.size()>1)
      {
        kept=false;
        break;
      }
    }
    if(kept) new_exprs.insert(x);
  }
  exprs=new_exprs;
  new_exprs.clear();

  for(auto &x: exprs)
  {
    bool red=false;
    /**
     * To check if ''x'' is identical with some
     * expr in ''new_exprs''. Two exprs ''x''
     * and ''y'' are identical iff they have the
     * same sign for every atomic condition ''c''.
     **/
    for(auto &y: new_exprs)
    {
      bool iden = true;
      for(auto &c : conditions)
      {
        std::set<signed> signs1=sign_of_expr(c, x);
        std::set<signed> signs2=sign_of_expr(c, y);
        int s1=signs1.size(), s2=signs2.size();
        // it is easy to check non-equivalence;
        if(s1!=s2)
        {
          iden=false;
          break;
        }
        else
        {
          if(s1==0 && s2==0) continue;
          // it is easy to check non-equivalence
          if(*(signs1.begin())!=*(signs2.begin()))
          {
            iden=false;
            break;
          }
        }
      }
      /**
       * If ''x'' is found identical w.r.t some
       * expr in ''new_conditions, we label it
       * and break.
       **/
      if(iden) 
      {
        red=true;
        break;
      }
    }
    // an expr is added into ''new_exprs''
    // if it is not found repetitive
    if(!red) new_exprs.insert(x);
  }

  // update the original ''exprs''
  exprs = new_exprs;
}

/*******************************************************************\

Function: eval_expr

  Inputs:

 Outputs:

 Purpose: To evaluate the value of expr ''src'', according to
          the atomic expr values

\*******************************************************************/
bool eval_expr(
  const std::map<exprt, signed> &atomic_exprs, 
  const exprt &src)
{
  std::vector<exprt> operands;
  collect_operands(src, operands);
  // src is AND
  if(src.id()==ID_and)
  {
    for(auto &x : operands)
      if(!eval_expr(atomic_exprs, x))
        return false;
    return true;
  }
  // src is OR
  else if(src.id()==ID_or)
  {
    std::size_t fcount=0;

    for(auto &x : operands)
      if(!eval_expr(atomic_exprs, x))
        fcount++;

    if(fcount<operands.size())
      return true;
    else
      return false;
  }
  // src is NOT
  else if(src.id()==ID_not)
  {
    exprt no_op(src);
    no_op.make_not();
    return !eval_expr(atomic_exprs, no_op);
  }
  else //if(is_condition(src))
  {
    // ''src'' should be guaranteed to be consistent
    // with ''atomic_exprs''
    if(atomic_exprs.find(src)->second==+1)
      return true;
    else //if(atomic_exprs.find(src)->second==-1)
      return false;
  }
}

/*******************************************************************\

Function: values_of_atomic_exprs

  Inputs: 

 Outputs:

 Purpose: To obtain values of atomic exprs within the super expr

\*******************************************************************/

std::map<exprt, signed> values_of_atomic_exprs(
  const exprt &e,
  const std::set<exprt> &conditions)
{
  std::map<exprt, signed> atomic_exprs;
  for(auto &c : conditions)
  {
    std::set<signed> signs=sign_of_expr(c, e);
    if(signs.size()==0)
    {
      // ''c'' is not contained in ''e''
      atomic_exprs.insert(std::pair<exprt, signed>(c, 0));
      continue;
    }
    // we do not consider inconsistent expr ''e''
    if(signs.size()!=1) continue;

    atomic_exprs.insert(
      std::pair<exprt, signed>(c, *signs.begin()));
  }
  return atomic_exprs;
}

/*******************************************************************\

Function: is_mcdc_pair

  Inputs: 

 Outputs:

 Purpose: To check if the two input controlling exprs are mcdc
          pairs regarding an atomic expr ''c''. A mcdc pair of
          (e1, e2) regarding ''c'' means that ''e1'' and ''e2''
          result in different ''decision'' values, and this is 
          caused by the different choice of ''c'' value.

\*******************************************************************/

bool is_mcdc_pair(
  const exprt &e1,
  const exprt &e2,
  const exprt &c,
  const std::set<exprt> &conditions,
  const exprt &decision)
{
  // An controlling expr cannot be mcdc pair of itself
  if(e1==e2) return false;
  
  // To obtain values of each atomic condition within ''e1'' 
  // and ''e2''
  std::map<exprt, signed> atomic_exprs_e1=
    values_of_atomic_exprs(e1, conditions);
  std::map<exprt, signed> atomic_exprs_e2=
    values_of_atomic_exprs(e2, conditions);

  // the sign of ''c'' inside ''e1'' and ''e2''
  signed cs1=atomic_exprs_e1.find(c)->second;
  signed cs2=atomic_exprs_e2.find(c)->second;
  // a mcdc pair should both contain ''c'', i.e., sign=+1 or -1
  if(cs1==0||cs2==0)
    return false;
  
  // A mcdc pair regarding an atomic expr ''c''
  // should have different values of ''c''
  if(cs1==cs2)
    return false;

  // A mcdc pair should result in different ''decision''
  if(eval_expr(atomic_exprs_e1, decision)==
     eval_expr(atomic_exprs_e2, decision))
    return false;

  /**
   *  A mcdc pair of controlling exprs regarding ''c''
   *  can have different values for only one atomic
   *  expr, i.e., ''c''. Otherwise, they are not
   *  a mcdc pair.
   **/
  int diff_count=0;
  auto e1_it=atomic_exprs_e1.begin();
  auto e2_it=atomic_exprs_e2.begin();
  while(e1_it!=atomic_exprs_e1.end())
  {
    if(e1_it->second!=e2_it->second)
    diff_count++;
    if(diff_count>1) break;
    e1_it++;
    e2_it++;
  }

  if(diff_count==1) return true;
  else return false;
}

/*******************************************************************\

Function: has_mcdc_pair

  Inputs: 

 Outputs:

 Purpose: To check if we can find the mcdc pair of the 
          input ''expr_set'' regarding the atomic expr ''c''

\*******************************************************************/

bool has_mcdc_pair(
  const exprt &c,
  const std::set<exprt> &expr_set,
  const std::set<exprt> &conditions,
  const exprt &decision)
{
  for(auto y1 : expr_set)
  {
    for(auto y2 : expr_set)
    {
      if(is_mcdc_pair(y1, y2, c, conditions, decision))
      {
        return true;
      }
    }
  }
  return false;
}

/*******************************************************************\

Function: minimize_mcdc_controlling

  Inputs: The input ''controlling'' should have been processed by
          ''collect_mcdc_controlling_nested''
                  and
          ''remove_repetition''

 Outputs:

 Purpose: This method minimizes the controlling conditions for
          mcdc coverage. The minimum is in a sense that by deleting
          any controlling condition in the set, the mcdc coverage
          for the decision will be not complete.

\*******************************************************************/

void minimize_mcdc_controlling(
  std::set<exprt> &controlling,
  const exprt &decision)
{
  // to obtain the set of atomic conditions
  std::set<exprt> conditions;
  for(auto &x: controlling)
  {
    std::set<exprt> new_conditions = collect_conditions(x);
    conditions.insert(new_conditions.begin(), new_conditions.end());
  }

  while(true)
  {
    std::set<exprt> new_controlling;
    bool ctrl_update=false;
    /**
     * Iteratively, we test that after removing an item ''x''
     * from the ''controlling'', can a complete mcdc coverage 
     * over ''decision'' still be reserved?
     *
     * If yes, we update ''controlling'' with the 
     * ''new_controlling'' without ''x''; otherwise, we should 
     * keep ''x'' within ''controlling''.
     *
     * If in the end all elements ''x'' in ''controlling'' are 
     * reserved, this means that current ''controlling'' set is 
     * minimum and the ''while'' loop should be breaked.
     *
     * Note:  implementaion here for the above procedure is 
     *        not (meant to be) optimal.
     **/
    for(auto &x : controlling)
    {
      // To create a new ''controlling'' set without ''x''
      new_controlling.clear();
      for(auto &y : controlling)
        if(y!=x) new_controlling.insert(y);

      bool removing_x=true;
      // For each atomic expr condition ''c'', to check if its is
      // covered by at least a mcdc pair.
      for(auto &c : conditions)
      {
        bool cOK=
          has_mcdc_pair(c, new_controlling, conditions, decision);
        /**
         *  If there is no mcdc pair for an atomic condition ''c'',
         *  then ''x'' should not be removed from the original 
         *  ''controlling'' set
         **/
        if(!cOK)
        {
          removing_x=false;
          break;
        }
      }

      // If ''removing_x'' is valid, it is safe to remove ''x'' 
      // from the original ''controlling''
      if(removing_x)
      {
        ctrl_update=true;
        break;
      }
    }
    // Update ''controlling'' or break the while loop
    if(ctrl_update)
    {
      controlling=new_controlling;
    }
    else break;
  }
}

/*******************************************************************\

Function: collect_decisions_rec

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void collect_decisions_rec(const exprt &src, std::set<exprt> &dest)
{
  if(src.id()==ID_address_of)
  {
    return;
  }

  if(src.type().id()==ID_bool)
  {
    if(src.is_constant())
    {
      // ignore me
    }
    else if(src.id()==ID_not)
    {
      collect_decisions_rec(src.op0(), dest);
    }
    else
    {
      dest.insert(src); 
    }
  }
  else
  {
    for(const auto & op : src.operands())
      collect_decisions_rec(op, dest);
  }
}

/*******************************************************************\

Function: collect_decisions

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

std::set<exprt> collect_decisions(const exprt &src)
{
  std::set<exprt> result;
  collect_decisions_rec(src, result);
  return result;
}

/*******************************************************************\

Function: collect_decisions

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

std::set<exprt> collect_decisions(const goto_programt::const_targett t)
{
  switch(t->type)
  {
  case GOTO:
  case ASSERT:
    return collect_decisions(t->guard);
  
  case ASSIGN:
  case FUNCTION_CALL:
    return collect_decisions(t->code);
    
  default:;
  }
  
  return std::set<exprt>();
}

/*******************************************************************\

Function: instrument_cover_goals

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void instrument_cover_goals(
  const symbol_tablet &symbol_table,
  goto_programt &goto_program,
  coverage_criteriont criterion)
{
  const namespacet ns(symbol_table);
  basic_blockst basic_blocks(goto_program);
  std::set<unsigned> blocks_done;
  
  // ignore if built-in library
  if(!goto_program.instructions.empty() &&
     has_prefix(id2string(goto_program.instructions.front().source_location.get_file()),
                "<builtin-library-"))
    return;

  const irep_idt coverage_criterion=as_string(criterion);    
  const irep_idt property_class="coverage";
  
  Forall_goto_program_instructions(i_it, goto_program)
  {
    switch(criterion)
    {
    case coverage_criteriont::ASSERTION:
      // turn into 'assert(false)' to avoid simplification
      if(i_it->is_assert())
      {
        i_it->guard=false_exprt();
        i_it->source_location.set(ID_coverage_criterion, coverage_criterion);
        i_it->source_location.set_property_class(property_class);
      }
      break;
      
    case coverage_criteriont::COVER:
      // turn __CPROVER_cover(x) into 'assert(!x)'
      if(i_it->is_function_call())
      {
        const code_function_callt &code_function_call=
          to_code_function_call(i_it->code);
        if(code_function_call.function().id()==ID_symbol &&
           to_symbol_expr(code_function_call.function()).get_identifier()==
           "__CPROVER_cover" &&
           code_function_call.arguments().size()==1)
        {
          const exprt c=code_function_call.arguments()[0];
          std::string comment="condition `"+from_expr(ns, "", c)+"'";
          i_it->guard=not_exprt(c);
          i_it->type=ASSERT;
          i_it->code.clear();
          i_it->source_location.set_comment(comment);
          i_it->source_location.set(ID_coverage_criterion, coverage_criterion);
          i_it->source_location.set_property_class(property_class);
        }
      }
      else if(i_it->is_assert())
        i_it->make_skip();
      break;
      
    case coverage_criteriont::LOCATION:
      if(i_it->is_assert())
        i_it->make_skip();

      {
        unsigned block_nr=basic_blocks[i_it];
        if(blocks_done.insert(block_nr).second)
        {
          std::string b=i2string(block_nr);
          std::string id=id2string(i_it->function)+"#"+b;
          source_locationt source_location=
            basic_blocks.source_location_map[block_nr];
          
          if(!source_location.get_file().empty() &&
             source_location.get_file()[0]!='<')
          {
            std::string comment="block "+b;
            goto_program.insert_before_swap(i_it);
            i_it->make_assertion(false_exprt());
            i_it->source_location=source_location;
            i_it->source_location.set_comment(comment);
            i_it->source_location.set(ID_coverage_criterion, coverage_criterion);
            i_it->source_location.set_property_class(property_class);
            
            i_it++;
          }
        }
      }
      break;
    
    case coverage_criteriont::BRANCH:
      if(i_it->is_assert())
        i_it->make_skip();

      if(i_it==goto_program.instructions.begin())
      {
        // we want branch coverage to imply 'entry point of function'
        // coverage
        std::string comment=
          "function "+id2string(i_it->function)+" entry point";

        source_locationt source_location=i_it->source_location;

        goto_programt::targett t=goto_program.insert_before(i_it);
        t->make_assertion(false_exprt());
        t->source_location=source_location;
        t->source_location.set_comment(comment);
        t->source_location.set(ID_coverage_criterion, coverage_criterion);
        t->source_location.set_property_class(property_class);
      }
    
      if(i_it->is_goto() && !i_it->guard.is_true())
      {
        std::string b=i2string(basic_blocks[i_it]);
        std::string true_comment=
          "function "+id2string(i_it->function)+" block "+b+" branch true";
        std::string false_comment=
          "function "+id2string(i_it->function)+" block "+b+" branch false";

        exprt guard=i_it->guard;
        source_locationt source_location=i_it->source_location;

        goto_program.insert_before_swap(i_it);
        i_it->make_assertion(not_exprt(guard));
        i_it->source_location=source_location;
        i_it->source_location.set_comment(true_comment);
        i_it->source_location.set(ID_coverage_criterion, coverage_criterion);
        i_it->source_location.set_property_class(property_class);

        goto_program.insert_before_swap(i_it);
        i_it->make_assertion(guard);
        i_it->source_location=source_location;
        i_it->source_location.set_comment(false_comment);
        i_it->source_location.set(ID_coverage_criterion, coverage_criterion);
        i_it->source_location.set_property_class(property_class);
        
        i_it++;
        i_it++;
      }
      break;
      
    case coverage_criteriont::CONDITION:
      if(i_it->is_assert())
        i_it->make_skip();

      // Conditions are all atomic predicates in the programs.
      {
        const std::set<exprt> conditions=collect_conditions(i_it);

        const source_locationt source_location=i_it->source_location;

        for(const auto & c : conditions)
        {
          const std::string c_string=from_expr(ns, "", c);
        
          const std::string comment_t="condition `"+c_string+"' true";
          goto_program.insert_before_swap(i_it);
          i_it->make_assertion(c);
          i_it->source_location=source_location;
          i_it->source_location.set_comment(comment_t);
          i_it->source_location.set(ID_coverage_criterion, coverage_criterion);
          i_it->source_location.set_property_class(property_class);

          const std::string comment_f="condition `"+c_string+"' false";
          goto_program.insert_before_swap(i_it);
          i_it->make_assertion(not_exprt(c));
          i_it->source_location=source_location;
          i_it->source_location.set_comment(comment_f);
          i_it->source_location.set(ID_coverage_criterion, coverage_criterion);
          i_it->source_location.set_property_class(property_class);
        }
        
        for(std::size_t i=0; i<conditions.size()*2; i++)
          i_it++;
      }
      break;
    
    case coverage_criteriont::DECISION:
      if(i_it->is_assert())
        i_it->make_skip();

      // Decisions are maximal Boolean combinations of conditions.
      {
        const std::set<exprt> decisions=collect_decisions(i_it);

        const source_locationt source_location=i_it->source_location;

        for(const auto & d : decisions)
        {
          const std::string d_string=from_expr(ns, "", d);
        
          const std::string comment_t="decision `"+d_string+"' true";
          goto_program.insert_before_swap(i_it);
          i_it->make_assertion(d);
          i_it->source_location=source_location;
          i_it->source_location.set_comment(comment_t);
          i_it->source_location.set(ID_coverage_criterion, coverage_criterion);
          i_it->source_location.set_property_class(property_class);

          const std::string comment_f="decision `"+d_string+"' false";
          goto_program.insert_before_swap(i_it);
          i_it->make_assertion(not_exprt(d));
          i_it->source_location=source_location;
          i_it->source_location.set_comment(comment_f);
          i_it->source_location.set(ID_coverage_criterion, coverage_criterion);
          i_it->source_location.set_property_class(property_class);
        }
        
        for(std::size_t i=0; i<decisions.size()*2; i++)
          i_it++;
      }
      break;
      
    case coverage_criteriont::MCDC:
      if(i_it->is_assert())
        i_it->make_skip();

      // 1. Each entry and exit point is invoked
      // 2. Each decision takes every possible outcome
      // 3. Each condition in a decision takes every possible outcome
      // 4. Each condition in a decision is shown to independently
      //    affect the outcome of the decision.
      {
        const std::set<exprt> conditions=collect_conditions(i_it);
        const std::set<exprt> decisions=collect_decisions(i_it);
        
        std::set<exprt> both;
        std::set_union(conditions.begin(), conditions.end(),
                       decisions.begin(), decisions.end(),
                       inserter(both, both.end()));

        const source_locationt source_location=i_it->source_location;

        for(const auto & p : both)
        {
          bool is_decision=decisions.find(p)!=decisions.end();
          bool is_condition=conditions.find(p)!=conditions.end();
          
          std::string description=
            (is_decision && is_condition)?"decision/condition":
            is_decision?"decision":"condition";
            
          std::string p_string=from_expr(ns, "", p);
        
          std::string comment_t=description+" `"+p_string+"' true";
          goto_program.insert_before_swap(i_it);
          //i_it->make_assertion(p);
          i_it->make_assertion(not_exprt(p));
          i_it->source_location=source_location;
          i_it->source_location.set_comment(comment_t);
          i_it->source_location.set(ID_coverage_criterion, coverage_criterion);
          i_it->source_location.set_property_class(property_class);

          std::string comment_f=description+" `"+p_string+"' false";
          goto_program.insert_before_swap(i_it);
          //i_it->make_assertion(not_exprt(p));
          i_it->make_assertion(p);
          i_it->source_location=source_location;
          i_it->source_location.set_comment(comment_f);
          i_it->source_location.set(ID_coverage_criterion, coverage_criterion);
          i_it->source_location.set_property_class(property_class);
        }
        
        std::set<exprt> controlling;
        //controlling=collect_mcdc_controlling(decisions);
        controlling=collect_mcdc_controlling_nested(decisions);
        remove_repetition(controlling);
        // for now, we restrict to the case of a single ''decision'';
        // however, this is not true, e.g., ''? :'' operator.
        minimize_mcdc_controlling(controlling, *decisions.begin());

        for(const auto & p : controlling)
        {
          std::string p_string=from_expr(ns, "", p);

          std::string description=
            "MC/DC independence condition `"+p_string+"'";
            
          goto_program.insert_before_swap(i_it);
          i_it->make_assertion(not_exprt(p));
          //i_it->make_assertion(p);
          i_it->source_location=source_location;
          i_it->source_location.set_comment(description);
          i_it->source_location.set(ID_coverage_criterion, coverage_criterion);
          i_it->source_location.set_property_class(property_class);
        }
        
        for(std::size_t i=0; i<both.size()*2+controlling.size(); i++)
          i_it++;
      }
      break;

    case coverage_criteriont::PATH:
      if(i_it->is_assert())
        i_it->make_skip();
      break;
    
    default:;
    }
  }

}

/*******************************************************************\

Function: instrument_cover_goals

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void instrument_cover_goals(
  const symbol_tablet &symbol_table,
  goto_functionst &goto_functions,
  coverage_criteriont criterion)
{
  Forall_goto_functions(f_it, goto_functions)
  {
    if(f_it->first==ID__start ||
       f_it->first=="__CPROVER_initialize")
      continue;
      
    instrument_cover_goals(symbol_table, f_it->second.body, criterion);
  }
}
