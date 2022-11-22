﻿#include "SUDSExpression.h"

#include "Misc/DefaultValueHelper.h"

PRAGMA_DISABLE_OPTIMIZATION

bool FSUDSExpression::ParseFromString(const FString& Expression, const FString& ErrorContext)
{
	// Assume invalid until we've parsed something
	bIsValid = false;
	Queue.Empty();
	
	// Shunting-yard algorithm
	// Thanks to Nathan Reed https://www.reedbeta.com/blog/the-shunting-yard-algorithm/
	// We take a natural string, parse it and then turn it into a queue of tokens (operators and operands)
	// expressed in Reverse Polish Notation, which can be easily executed later
	// Variables are not resolved at this point, only at execution time.
	
	// Split into individual tokens; sections of the regex are:
	// - {Variable}
	// - Arithmetic operators & parentheses
	// - Literal numbers (with or without decimal point)
	// - Boolean operators & comparisions
	// - Quoted strings (group 1 includes quotes, group 2 is trimmed)
	const FRegexPattern Pattern(TEXT("(\\{\\w+\\}|[-+*\\/\\(\\)]|\\d+(?:\\.\\d*)?|and|&&|\\|\\||or|not|\\<\\>|!=|!|\\<=?|\\>=?|==?|\\\"([^\\\"]*)\\\")"));
	FRegexMatcher Regex(Pattern, Expression);
	// Stacks that we use to construct
	TArray<ESUDSExpressionItemType> OperatorStack;
	bool bParsedSomething = false;
	bool bErrors = false;
	while (Regex.FindNext())
	{
		FString Str = Regex.GetCaptureGroup(1);
		ESUDSExpressionItemType OpType = ParseOperator(Str);
		if (OpType != ESUDSExpressionItemType::Null)
		{
			bParsedSomething = true;

			if (OpType == ESUDSExpressionItemType::LParens)
			{
				OperatorStack.Push(OpType);
			}
			else if (OpType == ESUDSExpressionItemType::RParens)
			{
				if (OperatorStack.IsEmpty())
				{
					UE_LOG(LogSUDS, Error, TEXT("Error in %s: mismatched parentheses"), *ErrorContext);
					bErrors = true;
					break;
				}
					
				while (OperatorStack.Num() > 0 && OperatorStack.Top() != ESUDSExpressionItemType::LParens)
				{
					Queue.Add(FSUDSExpressionItem(OperatorStack.Pop()));
				}
				if (OperatorStack.IsEmpty())
				{
					UE_LOG(LogSUDS, Error, TEXT("Error in %s: mismatched parentheses"), *ErrorContext);
					bErrors = true;
					break;
				}
				else
				{
					// Discard left parens
					OperatorStack.Pop();
				}
				
			}
			else
			{
				// All operators are left-associative except not
				const bool bLeftAssociative = OpType != ESUDSExpressionItemType::Not;
				// Valid operator
				// Apply anything on the operator stack which is higher / equal precedence
				while (OperatorStack.Num() > 0 &&
					// higher precedence applied now, and equal precedence if left-associative
					(static_cast<int>(OperatorStack.Top()) < static_cast<int>(OpType) ||
					static_cast<int>(OperatorStack.Top()) <= static_cast<int>(OpType)	&& bLeftAssociative))
				{
					Queue.Add(FSUDSExpressionItem(OperatorStack.Pop()));
				}

				OperatorStack.Push(OpType);
			}
		}
		else
		{
			// Attempt to parse operand
			FSUDSValue Operand;
			if (ParseOperand(Str, Operand))
			{
				bParsedSomething = true;
				Queue.Add(FSUDSExpressionItem(Operand));
			}
			else
			{
				UE_LOG(LogSUDS, Error, TEXT("Error in %s: unrecognised token %s"), *ErrorContext, *Str);
				bErrors = true;
			}
		}
	}
	// finish up
	while (OperatorStack.Num() > 0)
	{
		if (OperatorStack.Top() == ESUDSExpressionItemType::LParens ||
			OperatorStack.Top() == ESUDSExpressionItemType::RParens)
		{
			bErrors = true;
			UE_LOG(LogSUDS, Error, TEXT("Error in %s: mismatched parentheses"), *ErrorContext);
			break;
		}

		Queue.Add(FSUDSExpressionItem(OperatorStack.Pop()));
	}

	bIsValid = bParsedSomething && !bErrors;

	return bIsValid;
}


ESUDSExpressionItemType FSUDSExpression::ParseOperator(const FString& OpStr)
{
	if (OpStr == "+")
		return ESUDSExpressionItemType::Add;
	if (OpStr == "-")
		return ESUDSExpressionItemType::Subtract;
	if (OpStr == "*")
		return ESUDSExpressionItemType::Multiply;
	if (OpStr == "/")
		return ESUDSExpressionItemType::Divide;
	if (OpStr == "and" || OpStr == "&&")
		return ESUDSExpressionItemType::And;
	if (OpStr == "or" || OpStr == "||")
		return ESUDSExpressionItemType::Or;
	if (OpStr == "not" || OpStr == "!")
		return ESUDSExpressionItemType::Not;
	if (OpStr == "==" || OpStr == "=")
		return ESUDSExpressionItemType::Equal;
	if (OpStr == ">=")
		return ESUDSExpressionItemType::GreaterEqual;
	if (OpStr == ">")
		return ESUDSExpressionItemType::Greater;
	if (OpStr == "<=")
		return ESUDSExpressionItemType::LessEqual;
	if (OpStr == "<")
		return ESUDSExpressionItemType::Less;
	if (OpStr == "<>" || OpStr == "!=")
		return ESUDSExpressionItemType::NotEqual;
	if (OpStr == "(")
		return ESUDSExpressionItemType::LParens;
	if (OpStr == ")")
		return ESUDSExpressionItemType::RParens;

	return ESUDSExpressionItemType::Null;
}

bool FSUDSExpression::ParseOperand(const FString& ValueStr, FSUDSValue& OutVal)
{
	// Try Boolean first since only 2 options
	{
		if (ValueStr.Compare("true", ESearchCase::IgnoreCase) == 0)
		{
			OutVal = FSUDSValue(true);
			return true;
		}
		if (ValueStr.Compare("false", ESearchCase::IgnoreCase) == 0)
		{
			OutVal = FSUDSValue(false);
			return true;
		}
	}
	// Try gender
	{
		if (ValueStr.Compare("masculine", ESearchCase::IgnoreCase) == 0)
		{
			OutVal = FSUDSValue(ETextGender::Masculine);
			return true;
		}
		if (ValueStr.Compare("feminine", ESearchCase::IgnoreCase) == 0)
		{
			OutVal = FSUDSValue(ETextGender::Feminine);
			return true;
		}
		if (ValueStr.Compare("neuter", ESearchCase::IgnoreCase) == 0)
		{
			OutVal = FSUDSValue(ETextGender::Neuter);
			return true;
		}
	}
	// Try quoted text (will be localised later in asset conversion)
	{
		const FRegexPattern Pattern(TEXT("^\\\"([^\\\"]*)\\\"$"));
		FRegexMatcher Regex(Pattern, ValueStr);
		if (Regex.FindNext())
		{
			const FString Val = Regex.GetCaptureGroup(1);
			OutVal = FSUDSValue(FText::FromString(Val));
			return true;
		}
	}
	// Try variable name
	{
		const FRegexPattern Pattern(TEXT("^\\{([^\\}]*)\\}$"));
		FRegexMatcher Regex(Pattern, ValueStr);
		if (Regex.FindNext())
		{
			const FName VariableName(Regex.GetCaptureGroup(1));
			OutVal = FSUDSValue(VariableName);
			return true;
		}
	}
	// Try Numbers
	{
		float FloatVal;
		int IntVal;
		// look for int first; anything with a decimal point will fail
		if (FDefaultValueHelper::ParseInt(ValueStr, IntVal))
		{
			OutVal = FSUDSValue(IntVal);	
			return true;
		}
		if (FDefaultValueHelper::ParseFloat(ValueStr, FloatVal))
		{
			OutVal = FSUDSValue(FloatVal);	
			return true;
		}
	}

	return false;
	
}


FSUDSValue FSUDSExpression::Evaluate(const TMap<FName, FSUDSValue>& Variables) const
{
	checkf(bIsValid, TEXT("Cannot execute an invalid expression tree"));

	TArray<FSUDSExpressionItem> EvalStack;
	// We could pre-optimise all literal expressions, but let's not for now
	for (auto& Item : Queue)
	{
		if (Item.IsOperator())
		{
			FSUDSExpressionItem Arg1, Arg2;
			Arg1 = EvalStack.Pop();
			if (Item.IsBinaryOperator())
			{
				Arg2 = EvalStack.Pop();
			}
			EvalStack.Push(EvaluateOperator(Item.GetType(), Arg1, Arg2, Variables));
		}
		else
		{
			EvalStack.Push(Item);
		}
	}
	
	checkf(EvalStack.Num() == 1, TEXT("We should end with a single item in the eval stack and it should be an operand"));

	return EvaluateOperand(EvalStack.Top().GetOperandValue(), Variables);
}

FSUDSExpressionItem FSUDSExpression::EvaluateOperator(ESUDSExpressionItemType Op,
	const FSUDSExpressionItem& Arg1,
	const FSUDSExpressionItem& Arg2,
	const TMap<FName, FSUDSValue>& Variables) const
{
	const FSUDSValue Val1 = EvaluateOperand(Arg1.GetOperandValue(), Variables);
	FSUDSValue Val2;
	if (Arg1.IsBinaryOperator())
	{
		Val2 = EvaluateOperand(Arg2.GetOperandValue(), Variables);
	}

	switch (Op)
	{
	case ESUDSExpressionItemType::Not:
		return FSUDSExpressionItem(!Val1);
	case ESUDSExpressionItemType::Multiply:
		return FSUDSExpressionItem(Val1 * Val2);
	case ESUDSExpressionItemType::Divide:
		return FSUDSExpressionItem(Val1 / Val2);
	case ESUDSExpressionItemType::Add:
		return FSUDSExpressionItem(Val1 + Val2);
	case ESUDSExpressionItemType::Subtract:
		return FSUDSExpressionItem(Val1 - Val2);
	case ESUDSExpressionItemType::Less:
		return FSUDSExpressionItem(Val1 < Val2);
	case ESUDSExpressionItemType::LessEqual:
		return FSUDSExpressionItem(Val1 <= Val2);
	case ESUDSExpressionItemType::Greater:
		return FSUDSExpressionItem(Val1 > Val2);
	case ESUDSExpressionItemType::GreaterEqual:
		return FSUDSExpressionItem(Val1 >= Val2);
	case ESUDSExpressionItemType::Equal:
		return FSUDSExpressionItem(Val1 == Val2);
	case ESUDSExpressionItemType::NotEqual:
		return FSUDSExpressionItem(Val1 != Val2);
	case ESUDSExpressionItemType::And:
		return FSUDSExpressionItem(Val1 && Val2);
	case ESUDSExpressionItemType::Or:
		return FSUDSExpressionItem(Val1 || Val2);

		
	default: // these won't occur
	case ESUDSExpressionItemType::Null:
	case ESUDSExpressionItemType::Operand:
	case ESUDSExpressionItemType::LParens:
	case ESUDSExpressionItemType::RParens:
		return FSUDSExpressionItem();
	};
	
}

FSUDSValue FSUDSExpression::EvaluateOperand(const FSUDSValue& Operand,
	const TMap<FName, FSUDSValue>& Variables) const
{
	// Simplify conversion to variable values
	if (Operand.IsVariable())
	{
		if (const auto Var = Variables.Find(Operand.GetVariableNameValue()))
		{
			return *Var;
		}
		UE_LOG(LogSUDS, Error, TEXT("Variable state for %s missing, cannot evaluate"), *Operand.GetVariableNameValue().ToString());
	}

	return Operand;
}

PRAGMA_ENABLE_OPTIMIZATION
