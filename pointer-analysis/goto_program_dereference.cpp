/*******************************************************************\

Module: Dereferencing Operations on GOTO Programs

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include <irep2.h>
#include <migrate.h>
#include <simplify_expr.h>
#include <base_type.h>
#include <std_code.h>

#include "goto_program_dereference.h"

/*******************************************************************\

Function: goto_program_dereferencet::has_failed_symbol

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool goto_program_dereferencet::has_failed_symbol(
  const exprt &expr,
  const symbolt *&symbol)
{
  if(expr.id()=="symbol")
  {
    if(expr.invalid_object())
      return false;

    const symbolt &ptr_symbol=ns.lookup(expr);

    const irep_idt &failed_symbol=
      ptr_symbol.type.failed_symbol();

    if(failed_symbol=="") return false;

    return !ns.lookup(failed_symbol, symbol);
  }

  return false;
}

/*******************************************************************\

Function: goto_program_dereferencet::is_valid_object

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

bool goto_program_dereferencet::is_valid_object(
  const irep_idt &identifier)
{
  const symbolt &symbol=ns.lookup(identifier);

  if(symbol.type.is_code())
    return true;

  if(symbol.static_lifetime)
    return true; // global/static

  if(valid_local_variables->find(symbol.name)!=
     valid_local_variables->end())
    return true; // valid local

  return false;
}

/*******************************************************************\

Function: goto_program_dereferencet::dereference_failure

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_program_dereferencet::dereference_failure(
  const std::string &property,
  const std::string &msg,
  const guardt &guard)
{
  exprt guard_expr=guard.as_expr();

  if(assertions.insert(guard_expr).second)
  {
    guard_expr.make_not();

    // first try simplifier on it
    if(!options.get_bool_option("no-simplify"))
    {
      base_type(guard_expr, ns);
      simplify(guard_expr);
    }

    if(!guard_expr.is_true())
    {
      goto_programt::targett t=new_code.add_instruction(ASSERT);
      t->guard.swap(guard_expr);
      t->location=dereference_location;
      t->location.property(property);
      t->location.comment("dereference failure: "+msg);
    }
  }
}

/*******************************************************************\

Function: goto_program_dereferencet::dereference_rec

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_program_dereferencet::dereference_rec(
  exprt &expr,
  guardt &guard,
  const dereferencet::modet mode)
{
  expr2tc tmp_expr;
  migrate_expr(expr, tmp_expr);
  if(!dereference.has_dereference(tmp_expr))
    return;

  if(expr.is_and() || expr.id()=="or")
  {
    if(!expr.is_boolean())
      throw expr.id_string()+" must be Boolean, but got "+
            expr.pretty();

    unsigned old_guards=guard.size();

    for(unsigned i=0; i<expr.operands().size(); i++)
    {
      exprt &op=expr.operands()[i];

      if(!op.is_boolean())
        throw expr.id_string()+" takes Boolean operands only, but got "+
              op.pretty();

      expr2tc tmp_op;
      migrate_expr(op, tmp_op);
      if (dereference.has_dereference(tmp_op))
        dereference_rec(op, guard, dereferencet::READ);

      if(expr.id()=="or")
      {
        exprt tmp(op);
        tmp.make_not();
        guard.move(tmp);
      }
      else
        guard.add(op);
    }

    guard.resize(old_guards);

    return;
  }
  else if(expr.id()=="if")
  {
    if(expr.operands().size()!=3)
      throw "if takes three arguments";

    if(!expr.op0().is_boolean())
    {
      std::string msg=
        "first argument of if must be boolean, but got "
        +expr.op0().to_string();
      throw msg;
    }

    dereference_rec(expr.op0(), guard, dereferencet::READ);

    expr2tc tmp_op1, tmp_op2;
    migrate_expr(expr.op1(), tmp_op1);
    migrate_expr(expr.op2(), tmp_op2);
    bool o1 = dereference.has_dereference(tmp_op1);
    bool o2 = dereference.has_dereference(tmp_op2);

    if(o1)
    {
      unsigned old_guard=guard.size();
      guard.add(expr.op0());
      dereference_rec(expr.op1(), guard, mode);
      guard.resize(old_guard);
    }

    if(o2)
    {
      unsigned old_guard=guard.size();
      exprt tmp(expr.op0());
      tmp.make_not();
      guard.move(tmp);
      dereference_rec(expr.op2(), guard, mode);
      guard.resize(old_guard);
    }

    return;
  }

  if(expr.is_address_of() ||
     expr.id()=="reference_to")
  {
    // turn &*p to p
    // this has *no* side effect!

    assert(expr.operands().size()==1);

    if(expr.op0().id()=="dereference" ||
       expr.op0().id()=="implicit_dereference")
    {
      assert(expr.op0().operands().size()==1);

      exprt tmp;
      tmp.swap(expr.op0().op0());

      if(tmp.type()!=expr.type())
        tmp.make_typecast(expr.type());

      expr.swap(tmp);
    }
  }

  Forall_operands(it, expr)
    dereference_rec(*it, guard, mode);

  if(expr.id()=="dereference" ||
     expr.id()=="implicit_dereference")
  {
    if(expr.operands().size()!=1)
      throw "dereference expects one operand";

    dereference_location=expr.find_location();

    exprt tmp;
    tmp.swap(expr.op0());
    expr2tc tmp_expr;
    migrate_expr(tmp, tmp_expr);
    dereference.dereference(tmp_expr, guard, mode);
    tmp = migrate_expr_back(tmp_expr);
    expr.swap(tmp);
  }
  else if(expr.id()=="index")
  {
    if(expr.operands().size()!=2)
      throw "index expects two operands";

    if(expr.op0().type().id()=="pointer")
    {
      dereference_location=expr.find_location();

      exprt tmp("+", expr.op0().type());
      tmp.operands().swap(expr.operands());
      expr2tc tmp_expr;
      migrate_expr(tmp, tmp_expr);
      dereference.dereference(tmp_expr, guard, mode);
      tmp = migrate_expr_back(tmp_expr);
    }
  }
}

/*******************************************************************\

Function: goto_program_dereferencet::get_value_set

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_program_dereferencet::get_value_set(
  const exprt &expr,
  value_setst::valuest &dest)
{
  expr2tc new_expr;
  migrate_expr(expr, new_expr);
  value_sets.get_values(current_target, new_expr, dest);
}

/*******************************************************************\

Function: goto_program_dereferencet::dereference_expr

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_program_dereferencet::dereference_expr(
  exprt &expr,
  const bool checks_only,
  const dereferencet::modet mode)
{
  guardt guard;

  if(checks_only)
  {
    exprt tmp(expr);
    dereference_rec(tmp, guard, mode);
  }
  else
    dereference_rec(expr, guard, mode);
}

/*******************************************************************\

Function: goto_program_dereferencet::dereference

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_program_dereferencet::dereference_program(
  goto_programt &goto_program,
  bool checks_only)
{
  for(goto_programt::instructionst::iterator
      it=goto_program.instructions.begin();
      it!=goto_program.instructions.end();
      it++)
  {
    new_code.clear();
    assertions.clear();

    dereference_instruction(it, checks_only);

    for(goto_programt::instructionst::iterator
        i_it=new_code.instructions.begin();
        i_it!=new_code.instructions.end();
        i_it++)
      i_it->local_variables=it->local_variables;

    // insert new instructions
    while(!new_code.instructions.empty())
    {
      goto_program.insert_swap(it, new_code.instructions.front());
      new_code.instructions.pop_front();
      it++;
    }
  }
}

/*******************************************************************\

Function: goto_program_dereferencet::dereference

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_program_dereferencet::dereference_program(
  goto_functionst &goto_functions,
  bool checks_only)
{
  for(goto_functionst::function_mapt::iterator
      it=goto_functions.function_map.begin();
      it!=goto_functions.function_map.end();
      it++)
    dereference_program(it->second.body, checks_only);
}

/*******************************************************************\

Function: goto_program_dereferencet::dereference

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_program_dereferencet::dereference_instruction(
  goto_programt::targett target,
  bool checks_only)
{
  current_target=target;
  valid_local_variables=&target->local_variables;
  goto_programt::instructiont &i=*target;

  dereference_expr(i.guard, checks_only, dereferencet::READ);

  if(i.is_assign())
  {
    if(i.code.operands().size()!=2)
      throw "assignment expects two operands";

    dereference_expr(i.code.op0(), checks_only, dereferencet::WRITE);
    dereference_expr(i.code.op1(), checks_only, dereferencet::READ);
  }
  else if(i.is_function_call())
  {
    code_function_callt &function_call=to_code_function_call(to_code(i.code));

    if(function_call.lhs().is_not_nil())
      dereference_expr(function_call.lhs(), checks_only, dereferencet::WRITE);

    Forall_operands(it, function_call.op2())
      dereference_expr(*it, checks_only, dereferencet::READ);

    if (function_call.function().id() == "dereference") {
      // Rather than derefing function ptr, which we're moving to not collect
      // via pointer analysis, instead just assert that it's a valid pointer.
      exprt invalid_ptr("invalid-pointer", typet("bool"));
      invalid_ptr.copy_to_operands(function_call.function().op0());
      guardt guard;
      guard.move(invalid_ptr);
      dereference_failure("function pointer dereference",
                          "invalid pointer", guard);
    }
  }
  else if (i.is_return())
  {
    assert(i.code.statement() == "return");
    if (i.code.operands().size() == 0)
      return;

    assert(i.code.operands().size() == 1);

    exprt &ret = i.code.op0();
    dereference_expr(ret, checks_only, dereferencet::READ);
  }
  else if(i.is_other())
  {
    const irep_idt &statement=i.code.statement();

    if(statement=="decl")
    {
      if(i.code.operands().size()!=1)
        throw "decl expects one operand";
    }
    else if(statement=="expression")
    {
      if(i.code.operands().size()!=1)
        throw "expression expects one operand";

      dereference_expr(i.code.op0(), checks_only, dereferencet::READ);
    }
    else if(statement=="printf")
    {
      Forall_operands(it, i.code)
        dereference_expr(*it, checks_only, dereferencet::READ);
    }
    else if(statement=="free")
    {
      if(i.code.operands().size()!=1)
        throw "free expects one operand";

      exprt tmp(i.code.op0());

      dereference_location=tmp.find_location();

      guardt guard;
      expr2tc tmp_expr;
      migrate_expr(tmp, tmp_expr);
      dereference.dereference(tmp_expr, guard, dereferencet::FREE);
      tmp = migrate_expr_back(tmp_expr);
    }
  }
}

/*******************************************************************\

Function: goto_program_dereferencet::dereference

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_program_dereferencet::dereference_expression(
  goto_programt::const_targett target,
  exprt &expr)
{
  current_target=target;
  valid_local_variables=&target->local_variables;

  dereference_expr(expr, false, dereferencet::READ);
}

/*******************************************************************\

Function: goto_program_dereferencet::pointer_checks

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_program_dereferencet::pointer_checks(
  goto_programt &goto_program)
{
  dereference_program(goto_program, true);
}

/*******************************************************************\

Function: goto_program_dereferencet::pointer_checks

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void goto_program_dereferencet::pointer_checks(
  goto_functionst &goto_functions)
{
  dereference_program(goto_functions, true);
}

/*******************************************************************\

Function: remove_pointers

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void remove_pointers(
  goto_programt &goto_program,
  contextt &context,
  const optionst &options,
  value_setst &value_sets)
{
  namespacet ns(context);

  goto_program_dereferencet
    goto_program_dereference(ns, context, options, value_sets);

  goto_program_dereference.dereference_program(goto_program);
}

/*******************************************************************\

Function: remove_pointers

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void remove_pointers(
  goto_functionst &goto_functions,
  contextt &context,
  const optionst &options,
  value_setst &value_sets)
{
  namespacet ns(context);

  goto_program_dereferencet
    goto_program_dereference(ns, context, options, value_sets);

  Forall_goto_functions(it, goto_functions)
    goto_program_dereference.dereference_program(it->second.body);
}

/*******************************************************************\

Function: pointer_checks

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void pointer_checks(
  goto_programt &goto_program,
  const namespacet &ns,
  const optionst &options,
  value_setst &value_sets)
{
  contextt new_context;
  goto_program_dereferencet
    goto_program_dereference(ns, new_context, options, value_sets);
  goto_program_dereference.pointer_checks(goto_program);
}

/*******************************************************************\

Function: pointer_checks

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void pointer_checks(
  goto_functionst &goto_functions,
  const namespacet &ns,
  const optionst &options,
  value_setst &value_sets)
{
  contextt new_context;
  goto_program_dereferencet
    goto_program_dereference(ns, new_context, options, value_sets);
  goto_program_dereference.pointer_checks(goto_functions);
}

/*******************************************************************\

Function: dereference

  Inputs:

 Outputs:

 Purpose:

\*******************************************************************/

void dereference(
  goto_programt::const_targett target,
  exprt &expr,
  const namespacet &ns,
  value_setst &value_sets)
{
  optionst options;
  contextt new_context;
  goto_program_dereferencet
    goto_program_dereference(ns, new_context, options, value_sets);
  goto_program_dereference.dereference_expression(target, expr);
}
