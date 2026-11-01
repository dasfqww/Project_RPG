# RPG 보안과 Blueprint 연결 가이드

이 프로젝트의 보안 경계는 **서버 C++**, 밸런스와 연출은 **DataAsset/BP**가 담당한다. 클라이언트 BP가 제출한 피해량, 피격 목록, 위치를 그대로 신뢰하지 않는다.

## 1. 전역 정책 설정

1. Content Browser에서 `Miscellaneous > Data Asset`을 선택한다.
2. `RPGSecurityPolicy` 타입으로 `DA_SecurityPolicy_PvE`를 만든다.
3. 플레이어 Character BP의 `SecurityValidationComponent`에 이 에셋을 지정한다.
4. PvP가 생기면 별도의 `DA_SecurityPolicy_PvP`를 만들고 GameMode가 서버에서 `Set Security Policy`를 호출한다.

주요 항목:

- `Movement`: 서버 이동 샘플 간 거리·속도·불연속 허용치
- `Ability`: 초당 어빌리티 활성화 수와 동일 어빌리티 최소 간격
- `Combat`: 모든 스킬에 적용되는 최종 피해량 상한
- `Scoring`: 위반 점수 감쇠와 경고 임계값
- `Enforcement`: 단계별 상태 임계값, 회복 비율, 보호 동작 차단, 선택적 세션 제거

`On Violation Reported`, `On Risk Threshold Exceeded`, `On Enforcement State Changed`는 서버에서만 바인딩한다.

기본 enforcement 단계는 다음과 같다.

- `Monitoring`: 정상 관찰 상태
- `Elevated`: 추가 기록과 운영 관찰 대상
- `Restricted`: 플레이어 입력 Ability 및 BP 보호 동작 차단
- `Removal Recommended`: 세션 제거 권고. 자동 제거는 기본적으로 비활성화

상태가 내려갈 때는 `Recovery Ratio`를 적용해 임계값 주변에서 상태가 반복 전환되지 않게 한다. `Automatically Remove Player`는 충분한 Dedicated Server 오탐 측정 이후에만 활성화한다. 이 옵션은 현재 세션에서 Kick할 뿐 영구 계정 밴을 만들지 않는다.

거래·제작·보상 수령처럼 BP에서 시작하는 중요한 서버 동작은 실행 전에 `Can Perform Protected Action`을 호출한다. `Action Name`에는 `TradeCommit`, `CraftItem`, `RewardClaim`처럼 안정적인 이름을 사용한다. 반환값이 false면 동작을 중단하고 `Out Reason`을 서버 로그에 남긴다. 반복 거부 시 보고는 정책의 간격 제한을 받으므로 요청 폭주가 텔레메트리를 무한히 만들지 않는다.

기본 아이템 명령 컴포넌트의 `Equip`, `Unequip`, `Consume` Server RPC는 이 검사를
공통 진입점에서 자동 수행한다. 서버 Pawn에 `RPGSecurityValidationComponent`가 없으면
fail-closed로 거부하므로, 플레이어 BP에서 컴포넌트를 제거하거나 우회 RPC를 만들지
않는다. 거래·제작처럼 프로젝트별로 추가하는 BP RPC에는 위 노드를 계속 명시적으로
연결한다.

운영 BP가 세션 제거를 승인하면 `Request Player Removal`을 호출한다. `Ban Player`나 영구 제재 API에는 연결하지 않는다.

## 2. 스킬 DataAsset 설정

각 `RPGSkillDefinition`의 `Security` 섹션에서 다음을 지정한다.

- `Maximum Server Hit Distance`: 시전자에서 대상 서버 바운드까지의 절대 상한
- `Hit Location Tolerance`: 서버 충돌 바운드와 피격점의 허용 오차
- `Maximum Targets Per Query`: 한 번의 서버 HitQuery 결과 상한
- `Maximum Hits Per Activation`: 한 번 활성화 중 누적 적용 가능한 타격 상한
- `Maximum Damage Per Hit`: 트라이포드·모드 배율 적용 후 1회 피해 상한
- `Authorized Movement`: 대시·도약·텔레포트에만 활성화하는 시간/추가 거리 예산

Content Browser의 `Validate Assets`를 실행하면 유효하지 않은 보안 프로필은 에러로 표시된다.

## 3. 공격 Ability BP 연결

플레이어가 요청할 수 있는 `Local Predicted`와 `Server Only` Gameplay Ability는 모두
서버 활성화 빈도와 Restricted 상태 검사를 받는다. `On Spawn`/`On Given` 패시브 또는
서버가 직접 실행하는 반응형 Ability는 입력 요청으로 취급하지 않는다.

피해가 발생하는 AnimNotify 또는 실행 이벤트에서 `Execute Authorized Skill Damage`를 호출한다.

정책 기반 `SkillContainer`로 이전한 기존 BP는 `Shared.Event.Hit` / `Player.Event.AOE`
Notify를 그대로 사용할 수 있다. C++의 가상 피해 처리기가 기존 고정 반경 Trace 대신
현재 활성화의 `TargetingProfile` / `SecurityProfile`로 서버 HitQuery를 수행한다.
이벤트의 `Target`, `TargetData`, `EventMagnitude`는 피해 대상이나 피해량으로 사용하지 않는다.
기본 피해는 정의의 `SkillDataHandle` 행 `AttackPower`에서 RuntimeSpec의 `BaseDamage`로
활성화 시 한 번 고정한다. 행이 없거나 값이 유효하지 않으면 피해를 적용하지 않는다.
트라이포드/모드 `Shared.Stat.Damage` 배율은 그 고정 값에 적용된다. 기존 Action 기반
경로와 SkillContainer 밖의 레거시 Ability는 별도의 호환 경로이며, 자동 이전 대상으로
간주하지 않는다.

새 BP에서 승인 피해 노드를 직접 사용하는 경우에도 서버 RuntimeSpec의 `BaseDamage`를
연결한다. 동일 Notify에서 기존 피해 이벤트와 이 노드를 중복 호출하지 않는다.
대상별 무적 태그를 검사하며, 종료 중/종료 후와 GameplayEffect 콜백의 재진입을 거절한다.
누적 타격 예산은 효과 콜백 전에 예약하고, 콜백이 스킬을 종료·재시작하면 이전 타격
목록의 나머지를 적용하지 않는다. 판정에 실패한 대상은 승인 타격 예산을 소비하지 않는다.

`Maximum Damage Per Hit` 검사는 기본 피해 × RuntimeSpec 배율의 SetByCaller 값에
적용된다. 실제 HP 감소는 GameplayEffect의 공격력·방어력·치명타 계산 결과다.
이 입력 상한과 별도로 `SecurityProfile.MaximumFinalDamagePerHit`를 설정한다.
공격력·방어력·치명타 계산 뒤 이 값을 넘으면 피해를 줄여서 적용하지 않고 거절한다.
허용 범위는 1~1,000,000,000이며 NaN/Infinity도 거절한다. 기존 에셋의 호환 기본값은
기존 전역 한계인 1,000,000,000이다. 새 숫자를 에셋에 자동 저장하지 않으므로 정상
최대 치명타·버프·방어력 범위를 확인해 스킬별로 낮춰 설정해야 한다.

승인 피해 GE는 `Instant`, 정확히 하나의 네이티브 `GE_ExecCalc_TakeDamage` 실행,
직접 Modifier/추가 Execution/조건부·연결 효과 없음이 요구된다. 컴포넌트는 정확한
네이티브 AssetTags/TargetTags/TargetTagRequirements만 허용한다. 마지막 컴포넌트는
기존 공유 피해 GE의 무적 등 태그 조건을 검사하며 추가 피해를 실행하지 않는다.
커스텀 계산 클래스나 다른 컴포넌트,
지속/주기 효과는 이 승인 경로에서 거절한다. 부가 효과는 승인된 `Out Applied Hits`
이후 별도 경로로 설계한다. 임의 GE를 이 노드에 연결하면 자동으로 안전해지지 않는다.

최종 허용치는 소스 ASC·대상 ASC·GE 컨텍스트에 묶인 동기식 서버 내부 기록으로
실행 계산에 전달한다. 클라이언트/BP의 SetByCaller 항목으로 이 기록을 높일 수 없다.
GAS의 효과 적용 콜백이 발생하더라도 계산이 거절되면 성공 핸들/승인 대상이 반환되지
않으며 스킬 타격 예산이 반환된다. `OnGameplayEffectApplied`만 보고 피해 성공이나
보상을 처리하지 않는다. 이 계약 밖에서 서버 BP가 직접 ASC에 적용하는 효과는
스킬별 상한이 자동 적용되지 않는다(네이티브 TakeDamage 계산의 전역 상한만 적용).

ChargeSlash의 실제 BP 입력 → 몽타주 Notify → 승인 피해 → HP 복제 경로는
`Run-IrisMultiClientTest.ps1 -SkipBuild -VerifySkillDamage -ClientCount 1`로 재현한다.
2026-10-08 기본/지연·손실 실행에서 정상 적만 HP가 줄고 뒤쪽·아군·사거리 밖 대상은
그대로이며, 클라이언트의 실제 Attribute 변경 알림이 일치했다. 대상은 네이티브
픽스처이며 스킬/Rage 지급과 치명타 0은 테스트 준비다. 다른 스킬, 운영 NPC 생명주기,
악성 RPC의 실제 피해 시도, 여러 플레이어 간 피해 격리, 화면 UI 렌더링을 대신하지 않는다.
자세한 범위와 증거는 [보안 테스트 기록](SECURITY_ABUSE_TESTING.md#실제-charge-bp-타격과-hp-복제)을 참고한다.

입력:

- `Authorized Damage Effect Class`: 실제 체력 감소 GameplayEffect
- `Base Damage`: 서버가 알고 있는 스킬 기본 피해
- `Set By Caller Damage Tag`: 비워 두면 `Shared.SetByCaller.BaseDamage`

서버는 현재 활성화의 고정된 RuntimeSpec을 사용해 HitQuery를 다시 수행하고, 대상 수·거리·누적 타수·배율 적용 SetByCaller 피해 상한·적대 관계를 검증한다. `Out Applied Hits`는 검증을 통과해 실제 적용된 결과이므로, 서버 연출 복제나 후속 효과의 기준으로 사용한다.

다음 노드를 게임플레이 피해 경로에 직접 사용하지 않는다.

- 클라이언트가 만든 HitResult를 입력한 `Apply Gameplay Effect to Target`
- 클라이언트 계산 피해량을 받는 Server RPC
- 클라이언트 Overlap 결과를 그대로 받는 Server RPC

일반 NPC/보스처럼 SkillContainer 밖에서 서버가 직접 만든 HitResult를 처리할 때만 `Apply Authorized Server Damage`를 사용한다.

기존 BP 호환 경로도 서버 권위로 보강되어 있다. `Shared.SetByCaller.BaseDamage` 또는 `SetByCaller.BaseDamage`가 들어간 GameplayEffectSpec은 서버 전용 적용 함수에서 피해 스펙으로 인식되어 적대 관계·거리·피격점·피해 상한을 검사한다. 다만 새 BP는 대상 Actor만 넘기는 호환 노드보다 서버 Trace의 `HitResult`를 사용하는 승인 피해 노드를 우선한다.

레거시 플레이어 Ability는 Class Defaults의 `Skill > Legacy > Security`를 스킬별로 조정한다.

- `Legacy Server Direct Hit Distance`: 근접 공격의 서버 허용 사거리
- `Legacy Server Hit Location Tolerance`: 충돌체와 피격점 간 허용 오차
- `Legacy Maximum Targets Per Damage Event`: 회전베기 등 한 이벤트의 최대 대상 수
- `Legacy Maximum Damage Per Hit`: 해당 스킬의 실제 최고 피해보다 조금 높은 상한

호환 기본값은 기존 콘텐츠가 즉시 깨지지 않도록 넓게 잡혀 있다. 출시 값으로 그대로 사용하지 않는다.

## 4. 이동 Ability BP 연결

대시·도약 Ability에서 `Authorized Skill Movement Window` 태스크를 시작한다.

- `On Authorized`: 몽타주, Root Motion Source 또는 서버 이동을 시작
- `On Finished`: 정상 종료 후속 처리
- `On Cancelled`: Ability 취소 시 연출 정리
- `On Rejected`: 잘못된 프로필 또는 누락된 서버 보안 컴포넌트 처리

태스크는 클라이언트에서는 예측 연출만 시작하고, 서버에서만 제한된 시간과 총 추가 거리 예산을 연다. 완료·취소 시 예산은 자동 회수된다. 일반 텔레포트 BP 노드 앞뒤에서 수동 허가를 열어 둔 채 방치하지 않는다.

승인은 플레이어별 가장 최근 한 개를 사용하며 새 승인이 기존 승인을 교체한다.
각 AbilityTask는 생성한 승인의 고유 핸들을 보관하므로, 같은 `Reason`의 이전
태스크가 늦게 종료돼도 새 승인이나 이동 기준점을 지우지 않는다. `On Authorized`
콜백에서 즉시 태스크/Ability를 취소해도 승인과 타이머를 정리한다.

Ability 밖에서 서버 BP가 넉백이나 강제 이동을 실행할 때는 다음 흐름을 사용한다.

1. `Switch Has Authority`의 Authority 경로에서 대상의 `SecurityValidationComponent`를 얻는다.
2. `Begin Movement Authorization`에 짧은 시간, 필요한 **추가** 거리 예산과 `Reason`을 넣는다.
3. 반환한 GUID가 유효할 때만 `Launch Character` 또는 서버 이동을 실행하고 GUID를 보관한다.
4. 조기 종료/취소 시 보관한 GUID로 `End Movement Authorization`을 호출한다. 만료된 시간의 승인은 이동 샘플에서 자동 회수된다.

기간은 최대 10초, 추가 거리 예산은 최대 100000cm로 제한한다. 클라이언트 호출,
유한하지 않은 값, 0 이하 기간 또는 음수 예산은 무효 GUID를 반환하며 기존 승인을
바꾸지 않는다. GUID는 승인 결과를 구분하는 서버 내부 값이며 클라이언트의 권한
토큰으로 사용하지 않는다. 여러 효과가 같은 `Reason`을 사용해도 해제에는 이름
대신 저장한 GUID를 사용한다.

기존 `Authorize Movement Discontinuity` 노드는 호환용으로 유지된다. 이름으로
해제하는 `Cancel Movement Authorization`은 같은 이름의 최신 승인을 대상으로
하므로, 겹칠 수 있는 새 BP 작업에는 핸들 기반 노드를 사용한다. 일반 `Jump`에는
이동 면제를 열지 않는다. 클라이언트 점프는 CharacterMovement가 서버에서 검증한다.

실제 네트워크 회귀 검증은 [보안 공격 테스트 가이드](SECURITY_ABUSE_TESTING.md)의
`-VerifyAuthorizedMovement` 시나리오를 사용한다.

### 실제 구르기 BP 적용

`GA_Player_Roll`에는 기존 `ComputeRollDirectionAndDistance` 다음에
`Authorized Movement Window`를 연결했다. `On Authorized`에서만 기존
`PlayMontageAndWait`가 시작되고, `On Rejected`는 Ability를 종료한다.
정상 종료/블렌드아웃은 기존 `End Ability`를 유지하고, 몽타주의
`On Interrupted`/`On Cancelled`는 `End Interrupted Movement Ability`에 연결한다.
이 노드는 취소 결과로 Ability를 종료하되 취소 이벤트를 다시 방송하지 않는다.
`Cancel Ability` 방송 도중 몽타주 콜백에서 다시 취소를 방송하는 재귀를 피하며,
중단을 정상 종료로 기록하지 않는다. 기존 Motion Warping 계산은 유지한다.
GAS가 Ability를 끝내면 승인 태스크도 종료되어 고유 핸들로 승인을 회수한다.
Class Defaults의 `Net Security Policy`는 `Server Only Termination`으로 설정한다.
`Local Predicted` 실행은 유지하므로 입력 연출은 즉시 예측하지만, 클라이언트의
종료/취소 RPC가 서버 Ability의 수명을 단축하지 못한다. 지연된 활성화와 클라이언트
종료가 한꺼번에 도착해 서버 몽타주가 생략되는 경우도 막는다. 구르기의 최종 종료는
서버 몽타주 완료 또는 서버 취소가 결정한다. 다른 스킬의 종료 정책은 변경하지 않는다.

Class Defaults의 `RollMovementSecurityProfile`은 400cm 목표 거리와
`AM_Roll`의 약 0.833초 재생 길이를 기준으로 약 0.983초/추가 550cm를 사용한다.
이는 구르기 한 번의 총 추가 예산이며 매 샘플마다 새로 주는 거리가 아니다.
기준점 초기화는 끈다. 목표 거리나 몽타주 재생 속도를 바꾸면 프로필도 재검토한다.
BP 변수는 복제하거나 클라이언트 RPC의 입력값으로 받지 않는다.

`Scripts/Editor/ConfigurePlayerRollSecurity.py`는 기본 실행 시 원본 연결 또는
저장된 승인 연결을 검사하고 저장하지 않는다. 에디터를 닫고 원본 자산을 백업한
뒤 `-RPGApplyPlayerRollSecurity`를 붙여 실행하면 이 BP만 저장한다. 예상하지 못한
부모 클래스, 실행 연결, 목표 거리 또는 몽타주 설정이면 적용을 중단한다.
기존 승인 연결이 있으면 중복 노드를 추가하지 않는다.
이번 적용 전 원본은
`Saved/Backups/PlayerRollSecurity-d6727c793d8d47aaae6a8a360067bab7/GA_Player_Roll.uasset`에 보관했다.

`-VerifyAuthoredRoll` 네트워크 옵션은 실제 `IA_Roll` 입력, 저장된 구르기 BP,
`AM_Roll`과 서버 Root Motion을 검증한다. 다른 SkillContainer 스킬의 BP 그래프나
몽타주 설정을 변경하는 작업은 아니다.
`-ClientCount 2` 이상이면 플레이어별 결과 대조와 동시 구르기/취소 승인 격리
증거도 요구한다. 재현 명령과 실제 검증 범위는
[다중 플레이어 검사](SECURITY_ABUSE_TESTING.md#다중-플레이어-구르기-승인-격리)를 참고한다.

보안 틱은 메쉬의 `IsPlayingRootMotion()`으로 서버가 재생하는 Root Motion을
확인한다. `CharacterMovement.HasAnimRootMotion()`은 `PerformMovement()` 내부에서만
유효한 일시 상태이므로 독립된 보안 틱의 근거로 사용하지 않는다. 기본 점프와
추가 거리 예산 검증은 계속 유지한다.

## 5. Projectile과 지속형 공격 BP 연결

`RPGProjectileBase` 파생 BP는 Class Defaults의 `Projectile > Security`를 반드시 설정한다.

- 직선 투사체는 실제 최대 비행 거리와 서버 지연 여유를 합친 값으로 `Maximum Server Hit Distance`를 지정한다.
- 투사체 반경보다 조금 큰 값으로 `Hit Location Tolerance`를 지정한다.
- 치명타와 서버 버프를 포함한 정상 최대값보다 조금 큰 값으로 `Maximum Damage Per Hit`를 지정한다.

충돌과 피해 적용은 서버 복제본만 담당한다. 클라이언트 복제본은 피격 연출만 표시하며, `OnHit`와 `OnBeginOverlap` 모두 같은 서버 검증 경로를 사용한다. 한 투사체는 첫 유효 충돌만 처리한다.

Player 호환 계열은 다음 위치도 함께 설정한다.

- 근접 Ability의 `Maximum Server Damage Per Hit`
- Projectile Element의 `Security Profile`
- AOE Element의 `Security Profile`

Projectile과 AOE Element는 활성화별 타격 수와 중복 대상을 서버에서 제한한다. GameplayEffect가 면역 또는 적용 조건 때문에 거절되면 직접 Attribute 감소로 우회하지 않는다.

## 6. TargetData와 지면 지정 스킬

클라이언트가 보낸 TargetData는 조준 의도일 뿐 판정 결과가 아니다. 서버는 다음을 다시 검사한다.

- 단일 대상 스킬: 대상 수, 유효 Actor, 최대 거리, 서버 시야 Trace
- 지면 지정 스킬: 유한 좌표, 최대 거리, 서버 지면 Trace, 최종 위치까지의 시야

검증 실패 시 스킬 효과와 스폰을 수행하지 않는다. TargetData가 없는 자기 중심 AOE만 서버의 시전자 위치를 안전한 기본값으로 사용한다.

`RPGAbilitySystemComponent`가 서버 TargetData RPC의 캐시 생성 전에 미부여 스킬,
비활성/다른 활성화의 예측 키, 중복·역순 메시지와 과다 요청을 걸러낸다. 취소 RPC도
같은 요청 창과 활성화 검사를 사용한다. GAS의 Ability RPC Batch도 같은 구현으로
진입한다. 피해·거리·시야 검사를 대신하는 것이 아니라 그 앞의 전달 경계를 보호한다.

ASC Class Defaults의 `RPG > Security > TargetData > Maximum Target Data Requests Per Second`는
기본 64건이며 서버 소유 고정 1초 요청 창이다. 중복·거절·취소 요청도 집계하고, 창
경계에는 두 창의 버스트가 이어질 수 있다. 지속형 스킬의 정상 재타깃팅 빈도를
측정해 조정한다. 클라이언트 RPC에 이 제한값을 포함하지 않는다. 기본 ASC를 그대로
쓰는 캐릭터는 별도 BP 노드 연결 없이 방어가 적용된다.

한 요청은 최대 32개 TargetData 항목/합계 64개 Actor로 제한한다. 지원하는 네트워크
형식은 GAS `LocationInfo`, `ActorArray`, `SingleTargetHit`다. 커스텀 형식이 필요하면
명시적인 크기·유한값 검사를 추가한 뒤 지원한다. 동시에 추적하는 활성화는 최대
64개이고, 종료 시 예측 키 추적 상태를 제거한다. 순서는 GAS의 15비트 키 순환과
reliable ASC RPC 순서를 기준으로 한다. 키는 인증 토큰이나 영구 재전송 방지 ID가 아니다.

`SkillContainer`용 `FRPGSkillTargetDataCodec`은 기존 인코더와 같은 literal transform만
받는다. 클라이언트가 Actor/Socket transform 조회를 대신 지정할 수 없다. 기존
타기팅 정책의 서버 좌표·거리·조준·시야 검사는 계속 수행한다. 종료 뒤 늦게 도착한
데이터나 이전 활성화 데이터는 버리되 그것만으로 치트 위험 점수를 높이지 않는다.

Execution Policy를 사용하는 `SkillContainer`의 자연 종료는 서버가 결정한다.
클라이언트 예측 몽타주의 `EndAbility` 통지로 서버 실행을 종료하지 않는다. GAS는
서버가 입력 task를 기다릴 때 자연 종료 통지도 강제 취소로 바꿀 수 있으므로,
서버 ASC가 해당 통지를 처리하기 전에 차단한다. BP의 Net Security Policy 값을
일괄 변경하지 않아도 적용된다. 서버 몽타주 완료/정책 종료와 명시적인 GAS
`CancelAbility` 경로는 유지하며, 아직 Action 방식인 스킬과 다른 Ability는 변경하지 않는다.
클라이언트 자연 종료 통지는 정상 통신이므로 위험 점수를 올리지 않는다.
명시적 취소 자체의 악용 방지와 각 정책의 최장 실행 시간 검증은 별도 항목이다.

개발 검증용 `-VerifyTargetDataAbuse`는 별도 Iris 서버와 두 클라이언트에서 실제
TargetData RPC를 전송한다. PlayerController의 개발 전용 컴포넌트가 명시적인
`-RPGTargetDataAbuseE2E` 실행에서만 격리된 ASC 테스트 Actor를 생성한다. 기존
캐릭터 BP/스킬 에셋을 변경하거나 운영 환경에 변조 주입 노드를 연결할 필요가 없다.
Shipping에서는 컴포넌트를 부착하지 않으며 주입 헬퍼와 테스트 RPC 동작은 제외한다.
기본 환경과 각 방향 250~450ms/손실 10% 환경에서 2클라이언트의 실제 변조 12단계씩을
검증했다. 이 결과는 격리 ASC의 생산용 전달 경계에 대한 증거이며, 기존 BP의 실제
피해/HP/스폰 효과까지 검증한 것은 아니다. 새로운 BP/스킬 적용 시에는 정상 조준
입력과 서버 판정 효과의 통합 회귀를 별도로 수행한다.
검증 상태와 실행 명령은 [네트워크 변조 테스트](SECURITY_ABUSE_TESTING.md#targetdata-실제-iris-변조-테스트)를 참고한다.

`-VerifyTargetDataRate`는 같은 격리 ASC에 실제 96건 Confirm/Cancel 및 거절 요청 버스트를
보내고, 다른 클라이언트의 정상 요청과 제한 창 이후의 복구를 검사한다. BP의 요청 상한을
높이거나 개발용 공격 노드를 전투 그래프에 연결하지 않는다. 허용치 조정이 필요하면
정상 스킬 빈도를 먼저 측정하고 [실제 폭주 검증](SECURITY_ABUSE_TESTING.md#targetdata-실제-요청-폭주-검증)을
별도로 수행한다. 이 결과도 실제 피해·HP 효과 검증을 대신하지 않는다.

`-VerifyTargetDataDamage`는 같은 12단계 wire 검증을 **실제 서버 GE/HP**까지 확장한다.
두 연결이 두 피해 대상의 초기 HP를 받은 후 시작하며, 승인된 요청만 peer별 HP를 총 50 줄인다.
상대 대상 Actor를 제출해도 서버가 고정한 피해 대상을 바꾸지 않고, 중복/오래된 요청은 추가
피해를 만들지 않는다. 두 클라이언트에서 두 대상의 실제 HP 복제·변경 알림을 모두 요구한다.
BP에 테스트용 피해/준비 RPC 노드를 연결하거나 캐릭터 에셋을 바꿀 필요는 없다.

```powershell
.\Scripts\Network\Run-IrisMultiClientTest.ps1 -SkipBuild -VerifyTargetDataDamage -ClientCount 2 -TimeoutSeconds 180
```

Editor/정확히 두 클라이언트에서 다른 probe와 분리해 실행하며, 실행 전 모듈을 빌드한다.
기본 환경과 각 방향 250~450ms 지연/손실 10%에서 통과했다. 별도
`TargetDataDamage.proof.json`은 native hit fixture라는 범위를 명시한다. 실제 BP 몽타주/충돌
query를 통한 악성 요청 검증이나 생산용 플레이어·NPC 수명주기 검증을 대신하지 않는다.
[검증 범위와 기록](SECURITY_ABUSE_TESTING.md#targetdata-악성-wire-요청과-실제-hp-검증)을 참고한다.
Shipping에서는 이 준비 RPC와 피해 주입 본문이 동작하지 않는다.

`-VerifySkillTargetDataAbuse`는 격리된 native Ability가 아니라 기존
`GA_Player_ChargeSlash`와 실제 플레이어 ASC의 TargetData 소비 경로를 검사한다.
정상 입력 Press까지 확인한 뒤 재전송/다른 활성화/65 Actor 슬롯/잘못된 Actor 형식/
사거리 밖 좌표/예상하지 않은 이벤트 태그 등 7개 RPC를 보내고, 각 요청의 서버 HP 불변과
실제 스킬 검증을 확인한 다음 정상 입력을 해제해 기존 몽타주 피해가 복구되는지 검사한다.
새 테스트 노드를 BP에 붙이거나 RPC 제한·제재 점수를 낮추지 않는다.

```powershell
.\Scripts\Network\Run-IrisMultiClientTest.ps1 -SkipBuild -VerifySkillTargetDataAbuse -ClientCount 1 -TimeoutSeconds 180
```

Editor/한 클라이언트 전용이며 이번 코드가 반영된 DLL을 먼저 빌드해야 한다.
2026-10-09 실제 DLL에서 기본 환경과 각 방향 250~450ms 지연/패킷 손실 10% 모두 통과했다.
일곱 악성 요청마다 네 대상의 HP가 유지됐고, 정상 입력 해제 후 기존 BP 몽타주/GE가
적 한 명에게만 625 피해를 적용했으며 클라이언트 HP 변경 알림/복제도 확인했다.
같은 DLL의 native 보안 자동화 38개와 합성 증거/CLI 판정기 49개도 통과했다.
피해 대상은 native Pawn이므로 생산용 NPC 생명주기, 여러 플레이어의 동시 BP 스킬 격리,
당시 미검증이었던 BP 경유 최종 피해 상한 초과는 아래 별도 테스트로 확인한다.
전체 Shipping 실행은 아직 별도 검증이 필요하다.
[범위와 실행 증거](SECURITY_ABUSE_TESTING.md#실제-charge-bp의-악성-targetdata와-정상-해제-복구)를 참고한다.

두 플레이어의 동시 authored Charge 격리에는 별도 옵션을 사용한다.

```powershell
.\Scripts\Network\Run-IrisMultiClientTest.ps1 -SkipBuild -VerifySkillMultiPeerAbuse -ClientCount 2 -TimeoutSeconds 300 -MaxFPS 30
```

두 클라이언트의 양쪽 HP 준비와 두 실제 활성화의 Press 검증 뒤에 연결당 일곱 악성 요청을
보낸다. 상대 대상 Actor 위조, 요청마다 상대의 스킬 활성 상태/HP 불변, 두 정상 Release의
각각의 몽타주 피해와 양쪽 HP 복제를 검사한다. 새 BP 노드/자산 수정은 필요 없다.
추가 Press는 fixture가 기존 Enhanced Input Started 바인딩에 한 번 전달하며 별도 표식을
요구한다. 물리 키 재입력이나 최대 차지의 자동 해제를 정상 입력 해제 복구 증거로 세지 않는다.
공간적으로 분리된 정지 플레이어와 native 대상 fixture이므로 이동 보안, 다른 BP 스킬,
생산용 NPC 수명주기와 Shipping 실행의 증거를 대신하지 않는다.
2026-10-10 기본과 각 방향 250~450ms 지연/손실 10%에서 통과했다. 총 14개 악성 RPC 동안
양쪽 HP가 유지됐고, 정상 입력 해제 후 각 승인 적에만 625 피해가 적용돼 두 클라이언트에 복제됐다.
같은 DLL의 native 보안 회귀 38개와 기존 1인 BP 피해 회귀도 통과했다. 합성 판정기는 52개다.
Shipping은 변경 CPP 세 개의 `-NoLink` 실제 컴파일과 테스트 본문 비활성을 확인했지만,
전체 링크/패키지 실행은 아직 검증하지 않았다. BP 자산이나 운영 RPC 허용량/제재 임계값은 바꾸지 않았다.
[2인 검증 범위와 기록](SECURITY_ABUSE_TESTING.md#두-플레이어의-동시-charge-bp-악성-요청-격리)을 참고한다.

기존 Charge BP에서 계산 후 최종 HP 피해 상한을 검증하려면 다음 옵션을 사용한다.

```powershell
.\Scripts\Network\Run-IrisMultiClientTest.ps1 -SkipBuild -VerifySkillFinalCap -ClientCount 1 -TimeoutSeconds 300 -MaxFPS 30
```

Editor/한 클라이언트 전용이다. 테스트는 비활성 스킬 인스턴스의 Definition을 transient
복제해 상한만 625로 고정하고, 기존 입력/몽타주/Notify/서버 쿼리/GE를 실행한다. 정상 적의
625 피해는 승인되고, 방어력이 낮은 추가 적의 계산 피해 1,250은 HP 변경 전에 거절된다.
거절 대상의 서버/클라이언트 HP 불변뿐 아니라 적용 타격 목록과 소비 예산도 한 개임을
확인한다. 새 BP 노드나 자산 수정, 운영 보안 허용량 완화는 필요 없다.
2026-10-10 기본 환경과 각 방향 250~450ms 지연/손실 10%에서 같은 DLL로 통과했다.
새 `SkillFinalCap.proof.json`에만 상한 검증 범위를 추가하며 다른 악성 요청/Tripod/이동
테스트와 혼합하지 않는다. Shipping은 네 CPP의 실제 `-NoLink` 컴파일과 테스트 함수
여섯 개의 비활성을 확인했지만 전체 패키지 실행은 별도다. transient 테스트 상한은
운영 DataAsset 튜닝이나 악성 스탯 RPC/생산용 NPC 사망·리셋 검증을 대신하지 않는다.
[상한 경계 검증과 실행 증거](SECURITY_ABUSE_TESTING.md#실제-charge-bp의-최종-피해-상한-경계)를 참고한다.

Charge의 정상 종료 검증은 BP 자산 수정 없이 실제 서버 종료 통지와 클라이언트 자체 종료를
구분한다. 기존 GAS `ClientEndAbility` 호출 범위의 ASC/핸들/전체 예측 키를 실제 종료와
대조하므로, 서버 통지에 따른 `Replicate=0`을 무조건 거절하거나 무조건 허용하지 않는다.
서버 권위 종료 정책과 명시적 취소 동작은 유지한다. 새 피해 검증 옵션들은 출처 표식을
필수로 요구하므로 변경 코드가 반영된 Editor DLL을 먼저 빌드해야 한다.

실제 서버 통지가 아직 활성인 클라이언트를 끝내는 경로를 따로 검증하려면 다음을 실행한다.

```powershell
.\Scripts\Network\Run-IrisMultiClientTest.ps1 -SkipBuild -VerifySkillEndRace -ClientCount 2 -TimeoutSeconds 600 -MaxFPS 30
```

Editor/2인 전용의 명시적 테스트다. 클라이언트의 **실행 중 Release 몽타주만** 0.5배속으로
지연하며 서버 몽타주/GE, BP 자산/CDO, 운영 피해 프로필은 변경하지 않는다. 실제 GAS 종료
RPC가 양쪽에서 같은 활성화를 끝내야 성공한다. 결과의 `delayedClientMontageFixture`로
일반 실행과 구분하고, 일반 피해/상한 판정기로 지연 실행을 성공 처리하지 않는다.
수정 없는 환경에서의 자연 발생 빈도나 생산용 NPC 사망/리셋, 전체 Shipping 실행의
증거는 아니다. 프레임 차이 검증은 `-MaxFPS 60 -ClientMaxFPS 10`으로 별도 설정할 수 있으며
기본 `ClientMaxFPS=0`은 기존 MaxFPS를 상속한다.
[종료 출처 검증과 실행 기록](SECURITY_ABUSE_TESTING.md#charge-종료-출처와-무지연-2인-검증)을 참고한다.

공통 피해 GE는 공격자/대상의 **현재 서버 HP 및 사망 태그**도 검사한다. 유한한 양수 HP와
사망 태그 없는 ASC만 피해/아이덴티티 승인 경계로 진행하므로, 죽은 NPC에 반복 적용하거나
HP 0과 사망 태그 전파 사이에 끼어드는 공격은 거절된다. 기존 BP 자산 수정이나 신규 RPC는
필요 없다. BP에서 피해 성공/보상 분기를 연결할 때는 승인 API의 성공 반환/적용 대상 목록을
사용하고, 단순 GAS 효과 핸들을 실제 피해 성공으로 보지 않는다.
사망한 공격자의 사후 폭발·지속 피해가 필요하면 별도 서버 승인 계약을 설계해야 한다.

저장된 일반 NPC의 실제 startup·사망 몽타주·시체 공격 거절·서버 강제 교체 격리 검사는
다음으로 재실행한다. 신규 코드가 포함된 Editor DLL을 먼저 빌드해야 한다.

```powershell
.\Scripts\Tests\Run-NPCDamageLifecycleTest.ps1 -TimeoutSeconds 900
```

`BP_Dummy_Gdn_C`/`DA_Guardian`/`GA_Guardian_Death_C`/공통 BP 피해 GE를 그대로 사용한다.
이번에 주요 자산 7개는 변경 없이 유지됐고, 실제 사망/정상 몽타주 종료 및 시체 공격 3회
거절을 확인했다. 공격자는 native fixture, AI 이동은 정지, 교체는 서버 강제 생성이며 독립
월드에서 실행한다. 실제 NPC 네트워크 사망 복제나 자연 스포너, 같은 액터 부활/리셋,
드롭·보상 중복 정산을 완료했다는 의미는 아니다. 현재 일반 NPC의 in-place 리셋 API는 없고
플레이어 전용 presentation 복원 함수를 일반 NPC 리셋으로 재사용하지 않는다.
기존 Guardian BT 패키지/무기 태그 경고와 headless 피해 표시 경고는 숨기거나 자산을 덮어쓰지 않았다.
[실제 NPC 검증 범위와 실행 기록](SECURITY_ABUSE_TESTING.md#일반-npc의-실제-사망-bp와-강제-교체-격리)을 참고한다.

일반 NPC의 사망 상태는 서버에서 GAS 태그로 복제하고, NPC ASC는 Minimal 복제로
HP/태그/몽타주를 전달한다. AI Pawn에는 플레이어의 `OnRep_PlayerState` 초기화가 없으므로
NPC `BeginPlay`에서도 ASC actor info를 초기화한다. 클라이언트에서 HP나 사망 태그를
만들거나 사망 Ability를 직접 활성화해 서버의 결정을 대신하지 않는다.

`BP_RPGNPCBase`의 사망 그래프에는 전용 서버에서 Niagara async load만 건너뛰는
분기를 추가했다. 충돌 해제·dissolve timeline·무기 정리·드롭 활성화·액터 제거 연결은 유지했다.
수정 전 자산은 `Saved/Backups/NPCDeathPresentation-2f64bf90e83e4c5db8346d96545a0db5/`에 보존했다.
`Scripts/Editor/ConfigureNPCDeathPresentation.py`는 기본적으로 읽기 전용 검증이며,
명시적 `-RPGApplyNPCDeathPresentation` 없이 자산을 저장하지 않는다.

NPC의 실제 사망·서버 강제 교체를 2인 Iris 환경에서 검증하려면 다음을 실행한다.

```powershell
.\Scripts\Network\Run-IrisMultiClientTest.ps1 -SkipBuild -VerifyNPCDeath -ClientCount 2 -TimeoutSeconds 300 -MaxFPS 30
```

플레이어 입력이나 다른 스킬/이동 테스트와 혼합하지 않는 Editor 전용 검증이다.
저장된 NPC/사망 BP/피해 GE를 사용하며 native 공격자, 정지 AI, 고정 관찰 대기,
테스트 NPC의 넓은 가시거리 및 서버 강제 교체가 fixture 범위다. 실제 동작 관찰 없이
시간 경과만으로 성공하지 않는다. 자연 스포너나 같은 액터 부활, 드롭·보상 정산,
전체 Shipping 실행은 이 옵션으로 검증하지 않는다.

### 일반 NPC 사망 드롭 생성 경계 (2026-10-11)

`BP_Guardian_DropItemAbility`의 실행·보안 정책을 모두 `ServerOnly`로 저장했다.
기존 `InstancedPerExecution`, 테이블/Guardian 행 바인딩과 사망 그래프는 유지한다.
실제 드롭 호출은 사망 몽타주 종료 직후가 아니라 NPC의 **dissolve timeline 종료 후**이며,
드롭 Ability가 반환한 뒤 기존 BP가 액터를 제거한다. 이 순서를 테스트용으로 앞당기지 않는다.

네이티브 진입 경계는 현재 서버 NPC/ASC avatar 일치, 유한한 HP 0, 사망 태그와
액터 수명 동안의 1회 claim을 요구한다. claim은 SpawnActor/BP 콜백보다 먼저 예약해
재진입·추가 Ability spec·다시 활성화에 의한 재추첨을 막는다. HP/태그를 바꾸어도
claim은 초기화되지 않는다. 이는 서버 로컬 액터 경계이며 DB 영속 처치 ID나 부활/리셋 계약은 아니다.

테이블/행/행 타입, 최대 32개 엔트리, 유한한 확률 0~1, 유효한 pickup 클래스/manifest,
양수 수량 및 스택 상한을 전체 행에서 먼저 검증한다. 기존 BP 상호작용 수량이 `int8`이므로
드롭 수량은 1~127만 허용하고 더 큰 값은 거절한다(128 이상의 음수 변환/표시 불일치 방지).
기존 공용 BP 구조체의 필드 타입을 일괄 마이그레이션하지 않았다. 비스택 아이템은 수량 1만 허용한다.
확률 0은 비활성 엔트리이고 1은 확정이며, 잘못된 행을 부분적으로 생성하지 않는다.
수량은 Construction/BeginPlay/overlap 전에 실제 manifest에 초기화한다.
픽업 수령은 기존 `RPGPickupGrantComponent`/아이템 명령·트랜잭션 경로를 사용하며
클라이언트 지급 RPC나 별도 재화 변경 경로를 추가하지 않았다.

현재 저장된 `DT_ItemDrop`의 Guardian 드롭/보상 목록은 비어 있다. 보상 종류·확률·수량을
임의로 채우지 않았으므로 실제 NPC 처치→바닥 아이템 수령→HTTP/PostgreSQL 영수증의
전체 검증 완료로 표시하지 않는다. 확률 1의 실제 저장된 GreatSword pickup 생성 검사는
네이티브 fixture의 **일시적 테이블**이며 저장된 보상표를 변경하지 않는다.

정책 수정 전 BP는 `Saved/Backups/NPCDropAuthority-ab4f4ea49b3d48cd97b901a4df0ffa74/`에 보존했다.
`Scripts/Editor/ConfigureNPCDropAuthority.py`는 기본 읽기 전용이며
`-RPGApplyNPCDropAuthority`에서만 두 정책을 저장한다.
`Scripts/Editor/AuditNPCDropIntegration.py`는 그래프/정책/빈 Guardian 행을 읽기 전용으로 검사한다.

검증 기록은 서로 다른 빌드/범위를 합쳐 동일 DLL의 실행으로 표현하지 않는다.

- 아이템 자동화 56개 통과(새 NPCDrop 4개 포함):
  `Saved/Tests/Item/Automation-0cee5841fc6c47198dca45692d46cd89/Item.proof.json`, DLL `A6A472D4…`.
- 후속 Editor 모듈 링크 성공 및 지연 250~450ms/손실 10%의 2인 실제 NPC 사망 회귀 통과:
  `Saved/Logs/IrisMultiClient/20261011-092049-090d7be1/NPCDeathNetwork.proof.json`, DLL `B66582A9…`.
  양쪽 HP/태그/실제 몽타주/이전 액터 제거/강제 교체와 stale hit 거절을 확인했으며 드롭 정산 검사는 아니다.
- 이후 int8 수량 경계를 추가한 최종 Editor 모듈 링크와 직접 영향 NPCDrop 4개 재검증 통과:
  `Saved/Tests/NPCDropSecurity/Automation-9060015cc2764164a0763ab9607f65cc/NPCDropSecurity.proof.json`, DLL `F6752B6E…`.
  스택 상한 1000의 일시적 manifest도 128은 거절하고 127은 manifest/표시에 동일하게 반영했다.
  앞선 56개/2인 실행을 최종 DLL에서 반복했다는 의미가 아니다. 최초 링크 잠금은 공유 보스 실행을
  종료시키지 않고 해제 후 재링크해 해결했다(`NPCDropSecurity-EditorModule-20261011-QuantityBoundaryRetry.log`).
- Shipping 4개 SingleFile NoLink 컴파일, 개발 테스트 표식 부재 및 운영 guard 정의 유지 확인:
  `Saved/Tests/NPCDropSecurityShipping-20261011-QuantityBoundary/Shipping.proof.json`(최종 수량 경계 포함).
  전체 Shipping 링크/Cook/실행을 의미하지 않는다.

추가 standalone 관찰에서 실제 저장된 사망 BP의 드롭 활성화/종료/claim 소비는 각각 1회였다.
하지만 `Automation-2d8565e581f0426081a043d578ca5689` 실행은 NullRHI에서 Niagara 반환값이
없어 `Accessed None`을 기록했으므로 전체 성공으로 계산하지 않는다. 이후 `RenderOffscreen`
실행 `Automation-c0bd70a2f04440719333062b5ed1d589`은 초기 셰이더 컴파일 중 300초 timeout으로
중단됐다. 두 로그는 `Saved/Tests/NPCDamageLifecycle/`에 보존한다. 오류 판정은 완화하지 않았다.
기존 Guardian BT/누락된 텍스처 경고의 정리나 렌더링 콘텐츠 정상화는 이번 드롭 보안 변경에 포함하지 않는다.

## 7. 출시 전 운영 원칙

- Dedicated Server에서 패킷 지연·손실·고핑 환경을 포함해 허용치를 측정한다.
- `Risk Threshold` 초과는 곧바로 영구 밴하지 않고 세션 격리, 추가 로깅, 운영 검토 순으로 사용한다.
- 아이템·재화·거래는 기존 트랜잭션 서비스와 서버 저장소를 통해서만 변경한다.
- 치트 대응은 서버 권위 외에도 서버 로그 집계, 재현 가능한 감사 기록, 패치 가능한 정책 DataAsset을 함께 운영한다.
