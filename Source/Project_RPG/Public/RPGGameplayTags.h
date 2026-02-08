// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "NativeGameplayTags.h"

// 분리된 태그 헤더들을 포함합니다.
#include "RPGInputTags.h"
#include "RPGAbilityTags.h"
#include "RPGItemTags.h"
#include "RPGSharedTags.h"

/**
 * 마스터 헤더: 기존 코드와의 호환성을 위해 모든 태그 헤더를 통합합니다.
 * 컴파일 최적화를 원한다면 개별 헤더를 인클루드하는 것을 권장합니다.
 */
namespace RPGGameplayTags
{
    // 추가적인 전역 태그가 필요하다면 여기에 정의
}
