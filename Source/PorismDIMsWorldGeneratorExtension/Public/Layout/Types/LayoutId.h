// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "LayoutId.generated.h"

/** Request-owned identity text. Unlike FName, generated values do not enter the process-global name pool.
 * Equality, None and numbered-suffix ordering retain the layout identity contract; authored names may
 * enter this type, but there is deliberately no conversion back to an interned name.
 */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutId
{
	GENERATED_BODY()

	FLayoutId() = default;
	FLayoutId(const FName Name) : FLayoutId(Name.IsNone() ? FString() : Name.ToString()) {}
	FLayoutId(const EName Name) : FLayoutId(FName(Name)) {}
	FLayoutId(const TCHAR* Text) : FLayoutId(FString(Text)) {}
	FLayoutId(FString Text) : Value(MoveTemp(Text))
	{
		if (Value.Equals(TEXT("None"), ESearchCase::IgnoreCase)) Value.Reset();
	}

	/** Empty identity is the same sentinel as NAME_None. */
	bool IsNone() const { return Value.IsEmpty() || Value.Equals(TEXT("None"), ESearchCase::IgnoreCase); }
	const FString& ToString() const
	{
		static const FString None(TEXT("None"));
		return IsNone() ? None : Value;
	}

	/** Preserve FName's case-insensitive base-name order followed by numeric suffix order. */
	bool LexicalLess(const FLayoutId& Other) const
	{
		const FString& Left = ToString();
		const FString& Right = Other.ToString();
		int32 LeftLength = Left.Len(), RightLength = Right.Len();
		const uint32 LeftNumber = UE::Core::Private::ParseNumberFromName(*Left, LeftLength);
		const uint32 RightNumber = UE::Core::Private::ParseNumberFromName(*Right, RightLength);
		const int32 Order = FStringView(*Left, LeftLength).Compare(FStringView(*Right, RightLength), ESearchCase::IgnoreCase);
		return Order != 0 ? Order < 0 : LeftNumber < RightNumber;
	}

	friend bool operator==(const FLayoutId& A, const FLayoutId& B)
	{
		return A.ToString().Equals(B.ToString(), ESearchCase::IgnoreCase);
	}
	friend bool operator!=(const FLayoutId& A, const FLayoutId& B) { return !(A == B); }
	friend bool operator<(const FLayoutId& A, const FLayoutId& B) { return A.LexicalLess(B); }
	friend uint32 GetTypeHash(const FLayoutId& Id) { return GetTypeHash(Id.ToString()); }

private:
	/** Generated identity text owned by this value; editing it changes which request/artifact is referenced. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (AllowPrivateAccess = "true", ToolTip = "Owned request/artifact identity text. Changing it changes the referenced authority; do not substitute display labels."))
	FString Value;
};

template<> struct TStructOpsTypeTraits<FLayoutId> : TStructOpsTypeTraitsBase2<FLayoutId>
{
	enum { WithIdenticalViaEquality = true };
};
