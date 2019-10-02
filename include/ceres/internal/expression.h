// Ceres Solver - A fast non-linear least squares minimizer
// Copyright 2019 Google Inc. All rights reserved.
// http://code.google.com/p/ceres-solver/
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice,
//   this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
// * Neither the name of Google Inc. nor the names of its contributors may be
//   used to endorse or promote products derived from this software without
//   specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Author: darius.rueckert@fau.de (Darius Rueckert)
//
//
// This file contains the basic expression type, which is used during code
// creation. Only assignment expressions of the following form are supported:
//
// result = [constant|binary_expr|functioncall]
//
// Examples:
// v_78 = v_28 / v_62;
// v_97 = exp(v_20);
// v_89 = 3.000000;
//
//
#ifndef CERES_PUBLIC_EXPRESSION_H_
#define CERES_PUBLIC_EXPRESSION_H_

#include <string>
#include <vector>

namespace ceres {
namespace internal {

using ExpressionId = int;
static constexpr ExpressionId kInvalidExpressionId = -1;

enum class ExpressionType {
  // v_0 = 3.1415;
  COMPILE_TIME_CONSTANT,

  // For example a local member of the cost-functor.
  // v_0 = _observed_point_x;
  RUNTIME_CONSTANT,

  // Input parameter
  // v_0 = parameters[1][5];
  PARAMETER,

  // Output Variable Assignemnt
  // residual[0] = v_51;
  OUTPUT_ASSIGNMENT,

  // Trivial Assignment
  // v_1 = v_0;
  ASSIGNMENT,

  // Binary Arithmetic Operations
  // v_2 = v_0 + v_1
  PLUS,
  MINUS,
  MULTIPLICATION,
  DIVISION,

  // Unary Arithmetic Operation
  // v_1 = -(v_0);
  // v_2 = +(v_1);
  UNARY_MINUS,
  UNARY_PLUS,

  // Binary Comparision. (<,>,&&,...)
  // This is the only expressions which returns a 'bool'.
  // const bool v_2 = v_0 < v_1
  BINARY_COMPARISON,

  // General Function Call.
  // v_5 = f(v_0,v_1,...)
  FUNCTION_CALL,

  // The ternary ?-operator. Separated from the general function call for easier
  // access.
  // v_3 = ternary(v_0,v_1,v_2);
  TERNARY,

  // No Operation. A placeholder for an 'empty' expressions which will be
  // optimized out during code generation.
  NOP
};

// This class contains all data that is required to generate one line of code.
// Each line has the following form:
//
// lhs = rhs;
//
// The left hand side is the variable name given by its own id. The right hand
// side depends on the ExpressionType. For example, a COMPILE_TIME_CONSTANT
// expressions with id 4 generates the following line:
// v_4 = 3.1415;
class Expression {
 public:
  // These functions create the corresponding expression, add them to an
  // internal vector and return a reference to them.
  static ExpressionId MakeCompileTimeConstant(double v);
  static ExpressionId MakeRuntimeConstant(const std::string& name);
  static ExpressionId MakeParameter(const std::string& name);
  static ExpressionId MakeOutputAssignment(ExpressionId v,
                                           const std::string& name);
  static ExpressionId MakeAssignment(ExpressionId v);
  static ExpressionId MakeBinaryArithmetic(ExpressionType type,
                                           ExpressionId l,
                                           ExpressionId r);
  static ExpressionId MakeUnaryArithmetic(ExpressionType type, ExpressionId v);
  static ExpressionId MakeBinaryCompare(const std::string& name,
                                        ExpressionId l,
                                        ExpressionId r);
  static ExpressionId MakeFunctionCall(const std::string& name,
                                       const std::vector<ExpressionId>& params);
  static ExpressionId MakeTernary(ExpressionId c,
                                  ExpressionId a,
                                  ExpressionId b);

  // Returns true if the expression type is one of the basic math-operators:
  // +,-,*,/
  bool IsSimpleArithmetic() const;

  // If this expression is the compile time constant with the given value.
  // Used during optimization to collapse zero/one arithmetic operations.
  // b = a + 0;      ->    b = a;
  bool IsCompileTimeConstantAndEqualTo(double constant) const;

  // Checks if "other" is identical to "this" so that one of the epxressions can
  // be replaced by a trivial assignemnt. Used during common subexpression
  // elimination.
  bool IsReplaceableBy(const Expression& other) const;

  // Replace this expression by 'other'.
  // The current id will be not replaced. That means other experssions
  // referencing this one stay valid.
  void Replace(const Expression& other);

  // If this expression has 'other' as a parameter
  bool DirectlyDependsOn(ExpressionId other) const;

  // Converts this expression into a NOP
  void TurnIntoNop();

 private:
  friend class ExpressionTree;

  // Private constructor. Use the "Make__" functions instead.
  Expression(ExpressionType type, ExpressionId id);

  ExpressionId id_;

  // Depending on the type this name is one of the following:
  //  (type == FUNCTION_CALL) -> the function name
  //  (type == PARAMETER)     -> the parameter name
  //  (type == OUTPUT_ASSIGN) -> the output variable name
  //  (type == BINARY_COMPARE)-> the comparison symbol "<","&&",...
  //  else                    -> unused
  std::string name_;

  ExpressionType type_ = ExpressionType::NOP;

  // Expressions have different number of arguments. For example a binary "+"
  // has 2 parameters and a function call so "sin" has 1 parameter. Here, a
  // reference to these paratmers is stored. Note: The order matters!
  std::vector<ExpressionId> arguments_;

  // Only valid if type == COMPILE_TIME_CONSTANT
  double value_ = 0;
};

}  // namespace internal
}  // namespace ceres
#endif
