# 첫 성장 구간의 장비 강화·이전 실행 계약

작성일: 2026-10-31

상태: **강화/이전·증표8 선택 제작/+0 목표 장비 분해 API와 Memory/PG 원자 저장, backend 및 UE 서버 보상 정의/전송의 명시적 장비 단계 보존은 원본 구현/집중 확인 완료다. 재료/장비 패키지14파일은 원본 설치·read-only native 재로드를 마쳤다. 시작3종/+0 후보도 현재 보안 코드에 적용했고 실제 2인 신규 캐릭터의 UE/Iris 복원·보상/로비 복귀까지 완료했다. 기존 캐릭터/옛 아이템은 자동 변경하지 않으며 실행 중 서비스를 교체한 것은 아니다. UE 성장 명령/복구/UI/GAS·실제 보스 드롭/추첨·서비스 배포는 후속이다.**

UE 보존은10절, backend 저장은11절, 패키지는12절, 정의/계획은13절, 강화/이전 API는14절, 설치기는15절, 시작 지급 후보는16절, 제작/분해는17절, backend 정산 보상 단계는18절, UE/BP 정산 전송은19절, 원본 시작3종 적용/실제 연결은20절을 따른다. 초기 후보/미연결 기록은 당시 상태다. 독립 사본의 구현/검사·원본 적용·실제 UE 플레이/서비스 연결을 구분한다.

[첫 구간 수치안](PLAYER_PROGRESSION_FIRST_SLICE_V01.md#42-강화와-이전)의 확정 강화와 무료 전량 이전을 실제 명령으로 옮기기 위한 문서다. 수치안의 규칙은 유지하고, 새 저장 형식과 로비 명령 경로는 아래의 **구현 제안**으로 구분한다. 정식 서비스 정책으로 해석하지 않는다. 제작·분해의 실제 구현은17절을 따르고 개인별 장비 추첨은 후속이다.

## 1. 현재 구현에서 확인한 공백

| 확인한 코드 | 현재 제공하는 것 | 강화·이전에 추가로 필요한 것 |
|---|---|---|
| [아이템 Record](../Backend/ProjectRpg.Backend/Domain/ItemModels.cs)와 [UE Record](../Source/Project_RPG/Public/Item/Persistence/RPGItemRecord.h) | 기존 식별/Revision/위치·메타데이터와 선택 단계 보존. UE 소스 컴파일, backend mapper·Memory/PG·영수증·버전2 집중 확인 완료 | 실제 장비 정의 허용 목록·단계 초기화 명령, 정상 UE 모듈/HTTP·실제 저장 복원 연결 |
| [UE 액션 정책](../Source/Project_RPG/Public/Item/Policy/RPGItemActionPolicy.h)·[플래너](../Source/Project_RPG/Public/Item/Transaction/RPGItemActionCommandPlanner.h) | 장착·장착 해제·소모 등의 서버 명령 | 강화 가능 정의/부위/성장 구간 정책과 강화·이전 명령은 없음 |
| [일반 아이템 API](../Backend/ITEM_API_V2.md)·[Commit 서비스](../Backend/ProjectRpg.Backend/Application/ItemTransactionService.cs) | 게임 서버 전용 다중 Record CAS·영수증/재전송 대조. 플레이어는 별도 좁은 강화/이전 API를 사용 | UE의 새 요청/미확정 응답 복구와 화면 연결 |
| [장비 허용 목록](../Backend/ProjectRpg.Backend/Domain/FirstSliceEquipmentDefinitions.cs)·[변경 계획](../Backend/ProjectRpg.Backend/Domain/FirstSliceEquipmentCommandPlanner.cs) | 정확한7종 ID/버전·슬롯 해석·서버 조회 Record의 계획. 14절 API가 소유/영수증·리스 검사와 같은 저장 경계에서 호출 | 원본 장비/재료 설치·명시적 지급·UE/GAS 연결 |
| [시작 장비](../Backend/ProjectRpg.Backend/Domain/FirstSliceStarterEquipment.cs) | 20절에서 신규 시작3종/+0의 원본 적용·Memory/PG 원자성·실제 2인 UE 복원/정산/복귀 완료 | 실행 중 서비스 배포는 별도. 기존 캐릭터/옛 단계 미설정 아이템의 자동 전환은 없음 |
| [정산 보상](../Backend/ProjectRpg.Backend/Domain/DungeonSettlementModels.cs)·[지급 Worker](../Backend/ProjectRpg.Backend/Application/DungeonRewardSettlementWorker.cs) | 18·19절에서 backend 보존과 UE/BP 정의→스냅샷→JSON에 연결. 과거 누락 상태·fingerprint 유지 | 개인별 추첨/실제 보스 성장 장비 지급 연결 |
| [장비 스냅샷](../Source/Project_RPG/Private/Item/Equipment/RPGEquipmentSnapshot.cpp) | 저장된 StatValues를 복사하고 태그로 조회 | 강화 단계 해석과 공격력/최대 체력의 계산·적용 |
| [권위 장비 컴포넌트](../Source/Project_RPG/Private/Component/Equipment/RPGAuthoritativeEquipmentComponent.cpp)의 ApplyRecord | 정의의 AbilitySet 부여·액터 생성/회수 | 단계별 수치를 GAS에 반영·교체하는 경로. 스냅샷 복사만으로 수치안의 전투력이 적용되지는 않음 |

광석/증표와 장비의 원본 설치는 [설치 문서](PLAYER_PROGRESSION_PACKAGE_INSTALLATION.md), 현재 모듈의 read-only 재로드와 시작 지급은20절을 따른다. 정의 설치는 실제 광석 지급·비용 차감·강화 효과 완료를 뜻하지 않는다.

장비7종도 [Manifest](../ContentAuthoring/ProgressionEquipment/Manifest.json)에 실제 ID/버전·슬롯·참조·해시를 보관했다. 이후 13절에서 별도 서버 허용 목록을 이 결과와 대조했다. Manifest 자체를 신뢰해 클라이언트 요청을 승인하지 않는다.

## 2. 유지할 수치와 불변 조건

| 명령 | 입력 상태 | 성공 뒤 상태 | 비용 |
|---|---|---|---|
| 한 단계 강화 | 허용 장비 +0/+1/+2 | 같은 장비 +1/+2/+3 | 각각 광석 2/4/6 |
| 전량 이전 | 서로 다른 두 장비, 원본 +1~+3·대상 +0, 같은 부위/성장 구간 | 원본 +0·대상은 원본의 이전 단계 | 0 |

강화는 한 번에 한 단계만 진행한다. +3 초과·단계 건너뛰기·확률 실패·파괴·단계 하락·추가 통화는 없다. 세 부위 +3의 총비용은 광석 36개다. 이전은 합산/복제/광석 환급이 아니며 이미 강화된 대상·원본 +0·동일 ItemId는 거절한다. 귀속·소유자·위치·정의/인스턴스 ID·다른 수치와 잠금은 이전하지 않는다.

첫 구간의 장비와 비용은 캐릭터별이다. 명령 처리 시 두 장비와 모든 비용 스택은 인증된 같은 캐릭터 소유여야 한다. 이름 접두사·장비 카테고리만으로 대상을 허용하지 않고 정확한 Definition ID/Version이 등록된 성장 정책을 요구한다. 일반 대검 전체를 자동으로 첫 구간 장비에 포함하지 않는다.

구현 제안: 강화도 이전처럼 **로비 전용**으로 제한해 전투 도중 장비 능력치 변경 경로를 만들지 않는다. 수치안에 명시된 이전의 로비 제한을 유지하고, 강화의 이용 장소는 이 제안으로 명시한다. 여기서 로비는 화면 이름이 아니라 **유효한 던전 리스가 없는 캐릭터**다. Waiting 파티·입장/재접속 대기 등 유효한 리스가 남은 상태도 새 변경을 막는다.

## 3. 명령과 권한 경로 제안

로비 클라이언트 → 인증된 캐릭터별 강화/이전 명령 → backend의 소유·리스·성장 정책 검사/변경 계획 → 기존 아이템 저장소의 원자적 Commit/영수증 → 로비 재조회 → 다음 전투의 저장 장비 복원 순서로 연결한다.

일반 `POST /api/item-transactions/commit`은 계속 게임 서버 전용으로 둔다. 로비 기능 때문에 게임 서버 토큰을 클라이언트에 전달하거나 플레이어에게 일반 mutation 권한을 주지 않는다. backend에 별도 좁은 성장 명령을 추가하는 안이며, 장착/소모의 기존 UE 정책·API를 backend에 복제하는 작업은 아니다. 정확한 URL/HTTP 오류 스키마는 구현 시 기존 API 규약에 맞춘다.

| 클라이언트가 지정할 값 | 서버가 결정/검사할 값 |
|---|---|
| RequestId·대상 CharacterId·규칙 버전 | 인증 계정의 캐릭터 소유, 첫 구간 정책/규칙 버전, 새 변경 가능 여부 |
| 강화: ItemId·ExpectedRevision·예상 현재 단계·비용 스택 ID/ExpectedRevision 목록 | 실제 단계·정의/버전·광석의 정확한 안정 ID/버전/귀속·필요 수량·변경 Record |
| 이전: 원본/대상 ItemId·각 ExpectedRevision·각 예상 단계 | 실제 두 단계·서로 다른 ID·같은 부위/구간·+0 대상·변경 Record |

가격·새 단계·새 공격력/체력·새 owner·새 Record 전체를 클라이언트 입력으로 신뢰하지 않는다. 플레이어가 남의 영수증을 조회하지 못하도록 소유 확인을 영수증 반환보다 먼저 수행한다.

명령 fingerprint는 연산 종류·정규화한 캐릭터 ID·규칙 버전·대상/원본 ID와 역할·예상 Revision/단계·정렬한 비용 스택 ID/Revision을 포함한다. 비용 스택의 중복 ID는 거절하며, 이전의 원본/대상 순서는 역할이 달라 바꾸지 않는다. 소스 스택 집합을 바꾼 요청은 같은 RequestId의 재전송이 아니다. 서버 생성 결과의 해시만으로 요청 동일성을 대신하지 않는다.

## 4. 단계 저장과 호환성 제안

원본 단계는 **아이템 인스턴스의 명시적인 성장 상태**로 저장한다. `FirstSliceEquipmentProgressionState`는 `GrowthBandId="FirstSlice"`·`RuleVersion=1`·정수 `Level=0~3`을 표현한다. UE에는 10절의 선택 상태/JSON을, backend에는 11절의 `ItemRecord.State.EquipmentProgression`·HTTP 계약 변환·JSONB 저장/버전2 마이그레이션을 연결했다. 실제 게임 명령/서비스에는 아직 배포하지 않았다. 강화할 때 DefinitionVersion을 올리거나 GenerationSeed·CreationSource·수량·능력치에서 단계를 역산하지 않는다.

성장 정책은 Definition ID/Version마다 부위, 성장 구간, 기본 기여, 단계별 기여, 최대 단계와 비용을 해석한다. 인스턴스에는 현재 단계를 한 곳에 저장하고 다음 수치를 서버가 계산한다.

| 장비 | 시작 장비의 +0 기여 | 목표 장비의 +0 기여 | 단계당 기여 |
|---|---|---|---|
| 대검 | 0 | 공격력 +10 | 공격력 +3 |
| 상의 | 0 | 최대 체력 +100 | 최대 체력 +40 |
| 장갑 | 0 | 공격력 +4 | 공격력 +2 |

기존 임의 StatValues를 이 계산으로 덮어쓰지 않는다. 강화 단계·계산된 최종 수치 두 값을 각각 수정 가능한 원본으로 저장하면 불일치가 생기므로 피한다. 단계의 숫자를 전투 능력치 태그로 취급하거나 표시 문자열 `+2`로 저장하는 것도 피한다. 무기 성향의 보호막/투지 효과는 단계 이전 대상이 아니며 원래 장비 정의에 남는다.

새 필드 도입 시 backend JSON/DB·UE Record/HTTP 변환·저장 복원·장착/해제/이동·복구 영수증의 복사 경로가 필드를 보존해야 한다. 현재 일반 Record 검사만으로 새 성장 규칙이 검증되는 것은 아니다. 기존 영수증은 당시 형식으로 읽고 새 명령의 규칙 버전과 구분한다.

필드가 없는 기존 아이템을 일괄 +0으로 간주해 강화 가능하게 만들지 않는다. 첫 구간의 명시된 정의만 초기화/마이그레이션 대상으로 정하고, 미지원 규칙 버전·소수/음수/상한 초과 단계·성장 구간 불일치는 변경을 거절한다. 기존 대검과 새 시작/목표 장비의 대응·상의/장갑 지급은 그 작업에서 함께 결정한다.

## 5. 원자성·동시성·재전송

### 공통 처리 순서

1. 인증·소유·요청 형식/중복 ID를 확인한다. 동일 RequestId의 성공 영수증이 있으면 입력 전체를 대조해 원래 결과를 반환한다. 이때 현재 장비를 다시 강화하거나 새 비용 스택을 고르지 않는다.
2. **새 변경**은 리스 획득/세션 진입과 직렬화되는 경계 안에서 유효 리스 부재를 검사한다. 사전 GET 뒤 별도 item Commit만 수행하면 검사와 확정 사이에 입장할 수 있으므로 부족하다.
3. 관련 아이템의 실제 owner·위치·Active·unlocked·unexpired·정의/정책·Revision/단계를 검사하고 전체 변경을 계획한다. 장비 수량은 1이어야 한다. Terminal/메일/창고·다른 캐릭터/계정 소유의 재료를 자동 인출하지 않는다.
4. 장비 변경·재료 차감·성공 영수증을 **같은 저장 트랜잭션**에서 확정한다. 하나라도 실패하면 전체를 보존한다. Memory의 게임/아이템 게이트를 따로 잡아 순서 없이 호출하거나 PostgreSQL에서 서로 다른 연결로 리스/아이템을 확정하면 이 보장이 성립하지 않는다.
5. 성공/이미 확정 결과를 클라이언트에 반영한다. 현재 Revision보다 낮은 영수증의 Record로 최신 장비/재료를 덮어쓰지 않는다. 이후 장착·이동으로 상태가 바뀌었더라도 옛 요청에는 옛 성공 결과가 남는다.

현재 [구성 저장 Memory 코드](../Backend/ProjectRpg.Backend/Data/InMemoryGameRepository.Progression.cs)와 [PostgreSQL 코드](../Backend/ProjectRpg.Backend/Data/PostgresGameRepository.Progression.cs)는 캐릭터 소유·리스·명령 영수증의 참고 경로다. **성장 구성 저장과 아이템 변경을 이미 하나로 묶는 API는 아니다.** 실제 구현에서 세션 진입의 잠금 순서와 아이템 저장소의 공유 트랜잭션 경계를 맞춰야 한다.

### 강화 비용

광석 ID는 `RPGItemDefinition:GameItem.Material.Progression.Ore`, 정의 버전은 패키지의 1이다. 첫 구간에서는 해당 캐릭터의 Inventory에 있는 CharacterBound 스택만 비용 후보로 사용한다. 서버가 스택 ID를 정렬해 필요한 수량만 차감하며 잔량은 같은 스택에 남긴다. 전량 사용한 스택은 기존 소모 규칙처럼 수량 0·Terminal·Consumed로 남긴다. 미리 파괴한 뒤 장비만 별도 저장하지 않는다.

기존 일반 Commit 상한은 16 mutation이다. 강화 장비 1개를 제외한 비용 스택은 최대 15개이며, 요청이 상한을 넘거나 그 안에서 비용이 부족하면 **일부 단계/일부 재료를 확정하지 않고 거절**한다. 비용 6개 이하의 현재 수치에서도 구조적인 상한은 유지한다. 스택 합치기는 기존 기능을 통한 별도 확정 작업이며 강화 재시도 도중 임의로 새 스택을 끼워 넣지 않는다.

장비와 실제 차감된 모든 스택은 각 ExpectedRevision을 사용한다. 같은 광석을 쓰는 두 강화·동시 소모/이동 중 하나가 먼저 확정하면 나머지는 충돌로 끝나야 한다. 리스 확인, 장비 단계만 검사하고 광석의 CAS를 생략하는 구현은 허용하지 않는다.

### 이전과 미확정 응답

이전은 원본 +k→0과 대상 0→+k의 두 mutation 및 영수증을 같이 확정한다. 장비 위치를 바꾸거나 자동 장착하지 않는다. 다른 슬롯을 가리키는 같은 카테고리 장비도 허용하지 않으며 정책의 정확한 부위를 비교한다. 양쪽 중 하나가 동시에 이동/강화/잠금 변경돼 Revision이 달라지면 둘 다 보존한다.

타임아웃/연결 종료는 실패 확정이 아니다. 클라이언트는 RequestId와 전체 원래 명령을 보관하고 같은 명령으로 결과를 복구한다. 확인 전 새 RequestId를 발급하거나 비용을 돌려주지 않는다. 성공 뒤 응답을 잃고 던전에 들어간 경우에도 소유가 확인된 정확한 성공 재전송은 원래 영수증을 반환할 수 있다. 이는 새 강화 허용이 아니며 리스 검사로 성공 영수증까지 숨기지 않는다. 아직 커밋하지 않은 명령이 뒤늦게 들어오면 현재 유효 리스가 새 변경을 막는다.

## 6. 장착·전투 수치 반영 조건

영속 강화 성공과 GAS 반영을 별도 단계로 관리한다. 첫 구현에서는 로비에서 저장하고 다음 전투의 권위 장비 복원에서 단계/정책을 해석하는 경로를 우선한다. 클라이언트가 표시한 공격력을 서버에 보내 적용하지 않는다.

단계별 기여는 **현재 장착 중인 허용 장비**에서만 계산한다. 가방의 강화 장비·미장착 대검·같은 부위 복제본을 모두 합산하지 않는다. 장착/해제·같은 ItemId의 새 Revision·Pawn 교체/재접속에서 기존 장비 기여를 교체/회수해 중복 적용을 막는다. 기존 AbilitySet 부여/회수와 스폰 액터 수명도 유지한다. 구체적인 GameplayEffect/핸들 소유자는 구현 시 기존 장비 컴포넌트와 합의하며 새 Tick으로 전체 아이템을 반복 합산하지 않는다.

최대 체력 기여의 교체가 현재 체력을 자동으로 채우거나 죽은 Pawn을 살리지 않아야 한다. 감소 시 현재 체력의 상한 처리는 기존 GAS 생명/Attribute 정책과 함께 구현한다. 전투 시도 시작의 회복·투지/쿨타임 초기화와 장비 복원을 같은 사건으로 취급하지 않는다. 표시의 `+k` 변경이나 스냅샷 복제만 확인하고 공격력·최대 체력 적용 성공으로 보고하지 않는다.

## 7. 구현 순서와 완료 기준

| 단계 | 필요한 결과 | 그때 예약할 경로/담당 경계 |
|---|---|---|
| 1. 현재 준비물 설치 | 원본14파일 설치·현재 모듈 read-only 태그/ID/정의·보상 참조 재로드 완료 | 20절/설치 문서. 저장0·원본14 불변. 서비스 배포와 구분 |
| 2. 정의·저장 계약 | 순수 계산/저장·허용 목록 및 원본 신규 시작3종/+0 생성·동시 저장 완료. 실제 2인 UE 복원/정산/복귀 완료 | 20절. 기존 캐릭터/옛 아이템 자동 변경0·실행 중 서비스 교체0 |
| 3. 로비 명령 | 플레이어 API·소유 검사·과거 성공 사전 복구·리스/계획/Commit의 Memory/PG 직렬화 구현. HTTP/Memory7·PG 최종7그룹 확인 | 기존 범용 Commit은 게임 서버 전용. 기존 서비스/DB 배포와 UE 요청/UI 연결은 후속 |
| 4. UE 연결 | 로비 요청/미확정 잠금·재조회, 저장 단계 복원·현재 장비의 GAS 기여 교체 | 기존 온라인 아이템/장비/UI 담당과 순차 인계. 활성 전체 Source 예약 중 착수하지 않음 |
| 5. 관련 동작 확인 | 아래 사례의 상태/Revision/비용·전투 기여 관측 | 구현 뒤 변경 범위만 확인. 문서 작업 때문에 게임 빌드/실행하지 않음 |

필수 사례는 +0→1 광석2·+1→2 광석4·+2→3 광석6, +3/부족/잠금/만료/다른 owner 거절, 분할 스택 차감과 전량 tombstone, 두 장비 +k→0/0→+k의 원자성, 강화된 대상/다른 부위·구간 거절이다. 여기에 동일 성공 재전송·RequestId 내용 변경, 같은 광석의 동시 사용·이전 중 장비 이동, 입장과 강화의 경합, 성공 응답 유실 뒤 리스 활성, 예전 영수증 도착 뒤 최신 Revision 보존을 포함한다.

전투 완료 기준은 복원된 장착 장비만 반영하고 반복 복원/해제/재접속에서 기여가 누적되지 않으며 체력을 새로 채우지 않는 것이다. 이 사례들은 **앞으로 구현할 때의 기준**이다. 이번 문서 작업에서 Memory/HTTP/PG·UE/GAS·플레이 검사를 실행하거나 통과했다고 기록하지 않는다.

## 8. 첫 문서 작업의 범위

2026-10-31 최신 조율에서 훈련 1인 E2E 담당의 완료·해제를 확인했지만, platform-player-security 담당이 Source/정상 빌드 출력/Config/Content/Plugins를 현재 입력으로 예약했다. 이후 HUD 통합 확인과 훈련 개발 런처 담당도 독립 사본·Content 링크 입력 사용을 등록했다. 따라서 원본 재료 설치와 장비 코드 변경을 시작하지 않고 이 문서·본인 상태 문서만 편집했다. 훈련 완료/결과 UI의 별도 후보 패치와 사망 소비 접합도 해당 담당의 범위로 유지했다. 원본 설치 재개 시 한 담당의 종료만으로 충분하다고 가정하지 않고 그때의 활성 읽기 범위를 모두 다시 확인한다.

확인은 인용한 현재 코드·기존 증거와 문서 상대 링크/공백 대조에 한정한다. 게임/서버 실행·공유 빌드/정상 DLL 교체·에셋/설정/DB 수정은 없고 임시 실행 사본/캐시 생성·삭제는 0 bytes, 정리 예외도 없다. 다음 실제 작업은 최신 입력 사용 종료와 수정 범위를 다시 조율한 뒤 7절의 준비물 설치부터 진행한다.

## 9. 2026-10-31 순수 계산 규칙 구현

[FirstSliceEquipmentProgression.cs](../Backend/ProjectRpg.Backend/Domain/FirstSliceEquipmentProgression.cs)에 수치안의 계산 부분을 구현했다. `Upgrade`는 한 단계의 다음 상태·광석 비용·수치 기여를, `Transfer`는 원본 +0/대상 +k의 새 상태를 반환한다. 입력을 변경하지 않고 상태 전체를 검사하며 +3 강화·미지원 모델/성장 구간/규칙 버전·범위 밖 단계·다른 부위 이전·+0 원본·강화된 대상을 거절한다. `Contribution`은 시작 장비의 기본 기여 0과 목표 장비의 기본 기여, 단계별 추가량을 합산한다. 무기 두 성향의 숫자는 같고 보호막/투지 효과를 구현한 것은 아니다.

7종 `FirstSliceEquipmentModel`은 **서버 수치 레시피의 논리 구분**이며 실제 Definition ID 등록/허용 목록이 아니다. 새 명령 처리자는 저장 Record의 정확한 ID/버전을 허용 목록으로 해석한 뒤 이 계산을 호출해야 한다. 계산 입력에는 ItemId·owner·Revision·리스·광석 목록이 없으므로 같은 인스턴스/다른 캐릭터·중복 요청·재료 부족·저장 원자성을 이 코드가 검사한다고 보고하지 않는다. 그 책임은 3~5절의 실제 명령/저장 경로에 남는다. `Create()`는 명시적인 새 +0 도메인 값 생성일 뿐 기존 장비의 자동 마이그레이션이 아니다.

기존 [StarterEquipmentTests](../Backend/ProjectRpg.Backend.StarterEquipmentTests/Program.cs)에 `--equipment-progression-only` 분기를 추가하고 [새 집중 사례](../Backend/ProjectRpg.Backend.StarterEquipmentTests/EquipmentProgressionCases.cs)만 실행했다. 기본 실행의 기존 생성/성장 검사 본문은 유지했으며 그 suite를 반복 실행하지 않았다.

- 별도 `--artifacts-path`/출력 경로의 Release 빌드 종료 0·경고/오류 0, 집중 실행 종료 0·4그룹 통과.
- 7종의 2/4/6 비용·+3 상한, 문서의 100/1000·106/1000·120/1080·124/1180·129/1220 구성과 목표 +0의 114/1100, 두 무기 성향의 같은 기여를 확인했다.
- 7×7종·각 4단계의 **784개 이전 조합** 중 허용 51/거절 733과 허용 조합의 왕복·입력 불변, 잘못된 단계/구간/버전/모델 거절을 확인했다. 동일 모델의 이전 허용은 서로 다른 실제 장비일 때 사용할 계산이며 동일 ItemId 허용을 뜻하지 않는다.
- backend 빌드 입력 101개 전후 해시가 같고 기존 API/Record/저장소/생성 지급·DB/UE 소스·에셋/Config·정상 DLL/공유 빌드와 타 담당 실행은 변경하지 않았다. 실제 명령 경로에서는 아직 이 계산을 호출하지 않는다.

근거는 [Result.json](../Saved/BuildChecks/ProgressionEquipmentRules-20261031/Result.json)과 입력 해시/짧은 Build·Cases 로그다. 자기 Temp artifacts/bin/obj **5,151,627 bytes(4.91MiB)**를 finally에서 폐기해 잔여/예외가 없고, 모듈 해시는 당시 증거이며 폐기 DLL을 재실행 가능한 상태로 남기지 않았다. 검증 뒤 Saved 실행 명령의 preexisting Temp 보호를 소유 플래그로 보완했고 제품/사례 코드는 변경하지 않았다.

착수 때 platform 담당은 해제됐지만 airborne-platform-network가 UE 입력을 새 예약했고 훈련 런처도 Content 링크를 사용 중이었다. 원본 재료 설치는 계속 미착수이며 준비물 설치/실제 장비 정의 허용 목록·저장 형식/로비 명령의 후속을 최신 예약에 따라 진행한다.

## 10. 2026-10-31 UE 단계 보존 원본 적용 — 정상 DLL 미반영

[패치](../Saved/BuildChecks/ProgressionEquipmentTransport-20261031/EquipmentTransport.patch)를 처음에는 독립 후보로 작성했다. 소비 담당의 정상 NoLink 완료·전체 예약 해제 후 원본 11개 해시/문맥과 후보 11개를 재대조하고 실제 Source/공유 컴파일 범위를 새로 예약해 원본에 적용했다. [Readiness.json](../Saved/BuildChecks/ProgressionEquipmentTransport-20261031/Readiness.json)은 적용 전 준비 기록이고, 현재 결과는 [ImplementationResult.json](../Saved/BuildChecks/ProgressionEquipmentTransport-20261031/ImplementationResult.json)을 따른다.

| 원본 변경 | 내용 |
|---|---|
| `RPGItemRuntimeTypes.h/.cpp` | `FRPGEquipmentProgressionState`의 성장 구간·규칙 버전·정수 단계와 선택 상태를 보관. 기본 빈 값은 성장 미설정으로 두고 기존 아이템을 자동 +0 장비로 전환하지 않음 |
| `RPGItemRecord.cpp` | Active/Terminal 복원에서 잘못된 단계 값을 거절하고, 단계 있는 인스턴스의 수량이 1을 넘지 않도록 검사 |
| `RPGItemBackendJsonCodec.cpp` | `state.equipmentProgression`의 `growthBandId`/`ruleVersion`/`level` 보존. 누락/null은 기존 미설정 상태로 읽고, 잘못된 객체·지원하지 않는 구간/버전·소수/범위 밖 단계는 거절. 미설정 값은 전송에서 생략 |
| `RPGItemStackPolicy.cpp`·`RPGItemActionCommandPlanner.cpp` | 단계가 있는 인스턴스의 스택 병합 거절과 명시적 상태 재생성 경로의 값 보존 |
| `RPGInventoryProjectionTypes.h/.cpp`·`RPGInventoryProjectionMapper.cpp` | 소유자 읽기 모델의 값 복사·payload 비교·delta 복제에 단계를 포함 |
| `RPGEquipmentSnapshot.h/.cpp` | 현재 장비 스냅샷에도 단계를 전달. 숫자 전달이며 GAS 능력치/장비 성향 효과 적용은 아님 |

기존 이동/장착/소모의 Record 복사는 전체 상태를 유지하고, 확정 결과 대조는 reflected `CompareScriptStruct`를 사용하며 서버 journal은 JSON codec의 정규 본문을 보관한다. 새 UHT와 CPP의 NoLink 컴파일은 완료했지만 **정상 DLL 링크와 서버/클라이언트의 일관된 모듈 반영**은 남아 있다. 현재 설치 DLL을 새 필드의 실행 증거로 사용할 수 없다.

UE 작업 당시에는 backend 필드가 없었고, 그 보존 경로는 이후 11절에서 구현했다. 여전히 정상 UE 모듈/실제 명령 연결·서비스 배포가 남아 있으므로 UE 소스나 저장 필드만 보고 강화 값을 생성·지급하는 기능을 활성화하지 않는다. 10절의 변경에는 새 강화 RPC/API·광석 차감·승인 정책·재료/장비 정의 설치·기존 아이템 마이그레이션을 추가하지 않았다.

착수 재조회 사이 소비 담당이 원본 client 2개와 정상 타깃 NoLink/공유 UHT·UBT·Intermediate 입력을 예약했다. 초기에는 작은 자체 Candidate/패치/해시만 작성해 원본을 유지했고 문맥/공백을 확인했다. 그 담당의 명시적인 완료/해제 뒤 순차 적용·컴파일했으며, 소비/훈련/HUD 담당의 파일과 실행 중 고정 사본을 편집하거나 프로세스를 종료하지 않았다.

확인 결과는 정상 `Project_RPGEditor` 모듈의 **UHT/NoLink CPP 195개·종료 0·311.49초·오류 0**이다. 기존 `NonInstanced`와 로비 시험의 `SetViewportOverlayWidget` 폐기 예정 API 경고가 각 1건이며 관련 없는 코드는 수정하지 않았다. 컴파일 동안 Source/Config/Plugins 입력 1,795개와 보호 정상 바이너리 7개가 같고, 적용 11개도 후보와 같은 해시다. 링크/정상 DLL 교체·JSON 왕복/native·게임/HTTP/DB 검사는 수행하지 않았다.

Candidate 11개·patch·작성/컴파일 명령·작은 준비/입력 해시와 단일 Build 로그는 검토·인계물로 남긴다. 중복 Console 로그 **59,936 bytes(58.53KiB)**를 폐기했고 임시 OBJ/PCH/DLL/런타임·캐시 사본 생성은 0·정리 예외도 없다. 정상 Intermediate/UHT/객체는 유지할 빌드 출력이므로 삭제하지 않았다. Source/공유 UHT·UBT·Intermediate와 전체 읽기 입력 예약은 컴파일 후 해제했다. 이후 타 담당 변경은 당시 입력 불변 증거와 구분한다.

이후 backend 보존 연결은 11절을 따른다. 정상 UE 모듈 반영과 재료 원본 설치는 각 바이너리/Content 사용 담당의 종료·인계를 따로 확인해야 한다. 컴파일 완료를 실제 강화/저장 복원·능력치 적용 완료로 보지 않는다.

## 11. 2026-10-31 backend 단계 보존과 버전2 마이그레이션

기존 [Item API V2](../Backend/ITEM_API_V2.md#장비-단계의-선택-상태--2026-10-31)에 선택 `state.equipmentProgression`을 연결했다. 도메인/HTTP 계약의 누락/null은 기존 미설정 상태로 유지하고 JSON에서는 미설정 필드를 생략한다. 객체가 있으면 세 필드가 모두 필요하며 지원 구간/버전·0~3 정수 단계와 비중첩 수량을 검사한다. 기존 아이템/시작 지급에 자동으로 +0 상태를 붙이지 않는다.

Memory/PG Record·Revision 스냅샷·성공 영수증이 단계를 보존하고, 서비스의 정확한 재전송 비교에도 단계를 포함한다. 같은 RequestId/선언 fingerprint로 단계를 바꾸거나 생략하면 원래 성공 영수증과 충돌한다. 새 요청에서 단계가 있는 저장 아이템의 필드를 누락/null로 지우려 하면 전체 mutation과 성공 영수증 생성을 거절한다. 이동/Terminal 이력은 값을 유지하고 단계가 있는 아이템은 보상 스택과 병합하지 않는다. 이는 형식/저장 보호이며 새 강화 작업의 권한·비용·리스 승인이 아니다.

[0002_item_equipment_progression.sql](../Backend/Database/Migrations/0002_item_equipment_progression.sql)은 nullable JSONB 컬럼과 형식/값/수량 CHECK를 추가한다. **버전 1 `schema.sql`과 그 해시는 동결/불변**이다. Production 카탈로그는 두 버전을 명시적으로 검증/적용하고, Development/Testing 초기화와 직접 영향받는 fixture는 같은 전체 SQL을 기존 잠금/트랜잭션 안에서 재생한다. 서비스/기존 DB를 마이그레이션하거나 운영 프로세스를 재시작하지 않았다. [초기화 계약](../Backend/POSTGRES_SCHEMA_INITIALIZATION.md#장비-단계-델타와-버전2--2026-10-31)을 따른다.

검사는 기존 StarterEquipmentTests의 `--equipment-persistence-only`/`--equipment-postgres-only`와 기존 MigrationTests의 순수 모드로 한정했다. 입력/출력은 자체 artifacts 경로, DB는 캐시된 PG17 이미지의 소유 라벨·동적 loopback·tmpfs 컨테이너였다.

- 직접 영향받는 테스트 프로젝트 6개의 Release 빌드 종료 0·경고/오류 0. 전체 기존 테스트 suite 6개를 실행했다는 뜻은 아니다.
- 실제 mapper/JSON·Memory **5그룹**: 누락/null 호환, 0~3단계 변환/스냅샷/영수증 JSON, 잘못된 객체/부분 필드/소수/스택 거절, 누락 변경의 다중 아이템 원자적 거절·정확한 재전송/Terminal 보존, 패키지 버전 2 선택.
- 마이그레이션 순수/Memory provider 거절 **7개**: 카탈로그/이력 검증과 실제 배포 명령의 Memory 거절 등. 기존 MigrationTests PG/운영 역할 전체 경로는 반복하지 않았다.
- 실제 임시 PG **6그룹**: v1→v2·기존 NULL 행/no-op, 다중 아이템 누락 거절/영수증/Terminal, 새 연결 풀 재조회, Revision 경합의 승자 단계, 잘못된 JSON CHECK/실패 보존, Development 전체 초기화 반복.
- 빌드 입력 **139개**가 검사 전후·재개 감사 시 같고 v1 기준 파일도 동일했다. 검증 뒤 오래된 타입 설명 주석 2줄만 정정했으며 나머지 바이트/논리 코드는 같고 검사 재실행은 없다. 실제 HTTP/UE 왕복·정상 모듈/운영 DB·게임 플레이는 검사하지 않았다.

[ImplementationResult.json](../Saved/BuildChecks/ProgressionEquipmentBackend-20261031/ImplementationResult.json)이 최종 근거다. 초기 Result는 기능 검사 모두 성공 뒤 정리 예외로 false였다. 컨테이너 삭제가 빠져 종료된 자체 컨테이너가 남은 것을 재개 시 소유 ID/라벨/상태/tmpfs로 확인해 그 컨테이너만 삭제했다. 실행 스크립트에는 `--rm`·소유 컨테이너의 정지 후 삭제/대기 경로를 보완하고 구문을 확인했으며 통과한 검사는 반복하지 않았다.

자체 artifacts/DLL/obj **9,729,101 bytes(9.28MiB)** 폐기·Temp/컨테이너 잔여 0·최종 정리 예외 0이다. 작은 결과/명령/입력·모듈 해시/필수 로그만 남겼고 폐기 모듈 해시는 당시 증거다. Docker daemon/기반 이미지는 공유 자원으로 유지하며 기존 DB/컨테이너는 사용하지 않았다. 타 담당의 UE Source/공유 빌드/정상 DLL/에셋·설정·실행 입력/프로세스를 변경하지 않았다.

다음은 실제 장비 Definition/버전 허용 목록·초기 지급, 좁은 로비 강화/이전 명령과 소유/리스·광석 차감의 원자적 처리, UE 정상 모듈/실제 왕복·GAS 반영이다. 이 필드 확장으로 플레이어에게 일반 Commit을 열거나 비용 없는 단계 변경을 허용하지 않는다. 재료 원본 설치는 활성 Content 읽기 입력 종료 후 별도 재예약한다.

## 12. 2026-10-31 시작·목표 장비 정의 패키지

[CreateProgressionEquipmentAssets.py](../Scripts/Editor/CreateProgressionEquipmentAssets.py)로 네이티브 `RPGItemDefinition` 7개를 작성했다. 원본 Content를 읽는 HUD 취소 PIE와 겹치지 않게 새 대상 가지에만 쓰는 별도 프로젝트를 사용했다. 최종 결과는 `/Game` 스캔 밖의 [ProgressionEquipment](../ContentAuthoring/ProgressionEquipment/Manifest.json)에 보관하며 **원본 Content/Config에는 미설치**다. 기존 캐릭터 생성·대검 지급/장착과 다른 담당의 파일·프로세스는 변경하지 않았다.

아래 태그에 `RPGItemDefinition:`을 붙인 값이 실제 Primary Asset ID다. DefinitionVersion은 모두1, 경로는 `/Game/Blueprints/GameData/Item/Progression/DA_Item_Progression_<모델>`이다.

| 모델 | 안정 태그 | 호환 슬롯 |
|---|---|---|
| `StarterGreatSword` | `GameItem.Equipment.Progression.Starter.GreatSword` | Primary_R (9) |
| `StarterChest` | `GameItem.Equipment.Progression.Starter.Chest` | Chest (1) |
| `StarterGloves` | `GameItem.Equipment.Progression.Starter.Gloves` | Hands (4) |
| `EnduranceGreatSword` | `GameItem.Equipment.Progression.Target.EnduranceGreatSword` | Primary_R (9) |
| `CirculationGreatSword` | `GameItem.Equipment.Progression.Target.CirculationGreatSword` | Primary_R (9) |
| `TargetChest` | `GameItem.Equipment.Progression.Target.Chest` | Chest (1) |
| `TargetGloves` | `GameItem.Equipment.Progression.Target.Gloves` | Hands (4) |

모든 정의는 Equip·최대 스택1·임시 가방 크기1×1·정의가 소유한 Equipment fragment 한 개다. 무기3개는 기존 `DA_Item_GreatSword`의 GreatSword 유형·hand/utility 정책·`BP_PlayerWeapon_C`·`WeaponHandSocket`·상대 Transform·AbilitySet 목록을 재사용한다. 기존 목록은 비어 있으며 새 능력을 덧붙이지 않았다. 기존 작성기의 의도대로 hand는 숨김 `Count`이므로 임의의 TwoHand 정책을 만들지 않는다. 방어구4개는 Chest/Hands 슬롯과 네이티브 미지정 무기/utility 값·빈 장착 액터/AbilitySet을 유지한다.

아이콘·월드 픽업 Mesh/Class·Stat-roll fragment는 비어 있다. 기존 대검 픽업은 옛 아이템 ID를 지급하므로 재사용하지 않았다. 기본/단계별 공격력·체력은 backend 계산 규칙에 있고 이 에셋에 중복 수치나 GAS 효과를 넣지 않았다. 인내의 보호막·순환의 투지 효과와 실제 수치 적용은 후속이다. DataAsset은 인스턴스가 아니므로 `EquipmentProgression` 초기화·장비 지급·제작/분해도 실행하지 않았다.

작성기는 기본 읽기 전용, `-RPGCreateProgressionEquipmentAssets`에서 누락 정의만 생성한다. 기존 대상은 소유 marker·모델·전체 구조를 전부 사전 확인하고 다른 구성이면 쓰기 전에 거절한다. 기존 파일은 다시 저장하지 않는다. `-RPGProgressionEquipmentAssetsReload`는 새 프로세스에서 ID 등록을 엄격히 확인하고 저장하지 않는다. 태그 INI도 제공한다. marker는 작성 도구의 충돌 방지용이며 서버 권한 증거가 아니다.

작성/새 프로세스 재로드 **종료0/0·저장7/0**, 7종 실제 ID·파일 해시/속성 동일·dirty=false·네이티브 Valid/검증 오류·경고0이다. 최종 패키지는 에셋7·태그 INI·Manifest의 **9파일/52,936 bytes**이고 두 성공 로그의 Error/Fatal도0이다. 초기 PowerShell 목록 변환·Python 숨김 enum 변환과 hand를 반드시 지정해야 한다는 작성기 가정을 수정했으며 실패 시도와 최종 성공을 구분해 보존했다. 제품 C++/기존 에셋은 수정하지 않았다.

입력103개는 복사 전후 해시가 같은 사본으로 고정하고 복사 직후 원본 DLL/플러그인/Config 읽기 예약을 해제했다. 실제 사설 로드 DLL은 `9968DDA8…EA32034`이며 이 기존 모듈의 에셋 작성은 새 UE 선택 단계 필드의 정상 링크/실행 증거가 아니다. 기존 아이템 파일5개 불변·원본 새 대상/태그 파일 부재를 확인했다. 새 빌드·게임/HTTP/DB·지급 검사는 없다.

근거는 [ImplementationResult.json](../Saved/BuildChecks/ProgressionEquipmentAssets-20261031/ImplementationResult.json), Apply/Reload JSON·입력 해시·작은 결과/필수 로그다. 매 시도 자체 임시 프로젝트/복사 DLL/UserDir/캐시·중복 로그와 읽기 junction을 정리했다. 총 **110,492,338 bytes(105.37MiB)** 폐기·Temp 잔여/최종 예외0이며 공유 DDC는 정리 대상에 포함하지 않았다. 폐기 DLL 해시는 당시 증거로만 남긴다. 다음은 활성 Content 독자가 모두 종료/인계된 뒤의 원본 설치, 실제 ID/버전의 서버 허용 목록·명시적 시작 장비 지급과 좁은 강화/이전 명령의 원자적 차감이다. 매 단계 최신 조율표·호출 코드를 재확인해 별도 예약한다.

## 13. 2026-10-31 정확한 장비 정의와 강화·이전 변경 계획

[FirstSliceEquipmentDefinitions.cs](../Backend/ProjectRpg.Backend/Domain/FirstSliceEquipmentDefinitions.cs)는 12절의 7종에 정확한 DefinitionType/Name/Version1·논리 모델·장착 슬롯을 대응한다. 접두사/카테고리로 추측하거나 다른 버전을 대체하지 않는다. 옛 `GameItem.Equipment.Weapon.GreatSword`는 포함하지 않는다. 광석의 정확한 ID/Version1·최대999도 지정했고 실제 장비/재료 Manifest와 대조했다. 이 단계에서는 HTTP 처리가 없었고 이후 14절에서 연결했다. 기존 범용 Record 형식 검사·게임 서버 Commit 권한·시작 지급은 변경하지 않았다.

[FirstSliceEquipmentCommandPlanner.cs](../Backend/ProjectRpg.Backend/Domain/FirstSliceEquipmentCommandPlanner.cs)는 RequestId/CharacterId/규칙 버전·장비 ID/ExpectedRevision/예상 단계와 비용 스택 ID/ExpectedRevision을 받는다. 별도 인자는 서버가 조회한 Record 사전과 서버 시각이다. 입력 Record를 변경하지 않고 기존 저장소의 `ItemRepositoryCommitRequest`를 반환한다. 이 사전을 클라이언트가 제출하는 경로를 만들지 않는다.

계획 전 실제 Record의 ID/형식·Revision·캐릭터 owner/컨테이너 소유·Active·CharacterBound·잠금·만료를 검사한다. 장비는 Inventory 또는 정의에 맞는 Equipment 슬롯·수량1·명시적으로 초기화된 지원 단계가 필요하다. 빈 단계의 옛 아이템을 +0으로 바꾸지 않는다. 광석은 정확한 정의/버전·Inventory·1~999·성장 상태 없음만 허용한다. 장비와 비용의 중복 ID·0/상한 Revision·예상 단계 불일치·지원하지 않는 규칙을 거절한다.

- 강화: 광석 후보1~15개를 안정적인 ItemId 문자열 순서로 정렬해 한 단계의2/4/6개를 차감한다. 부족하면 계획 전체를 거절한다. 전량 차감 스택은 수량0·Terminal/Consumed로, 나머지는 원래 위치의 잔량으로 남긴다. 장비+실제 차감 스택만 mutation에 포함하고 각 ExpectedRevision과 기존16 mutation 상한을 지킨다. 사용하지 않은 후보의 변경을 쓰거나 부족한 비용을 임의의 새 스택에서 찾지 않는다.
- 이전: 서로 다른 같은 논리 부위의 원본+k/대상0을 원본0/대상+k 두 mutation으로 만든다. 장착 위치·아이템 ID·정의·귀속·잠금·내구도·다른 instance 태그/수치를 그대로 보존한다. 합산/복제/자동 장착·재료 지급은 없다.
- fingerprint: 현재 아이템을 읽지 않고 원래 명령에서 계산할 수 있다. 캐릭터/RequestId·방향·규칙·ID·Revision·예상 단계·전체 비용 후보를 포함하고, 비용 후보의 입력 순서만 정규화한다. 인벤토리의 현재 상태에서 새 비용을 골라 옛 요청을 재구성하는 수단이 아니다.

**계획 생성은 로비 권한/리스 승인이나 Commit 완료가 아니다.** 후속 처리자는 인증/캐릭터 소유를 확인하고, 정확한 과거 성공 영수증을 현재 상태 검사보다 먼저 복구해야 한다. 새 변경은 입장 리스/아이템 쓰기와 같은 Memory gate 또는 PostgreSQL 트랜잭션 안에서 조회·계획·Commit해야 한다. 지금의 계획 뒤에 단순히 별도 리스 GET과 일반 Commit을 붙여 공개하면 안 된다. 이 작업은 새 API·인증·리스 저장소/PG 트랜잭션 경계를 추가하지 않았다.

기존 StarterEquipmentTests에 `--equipment-commands-only`를 추가해 [집중 사례](../Backend/ProjectRpg.Backend.StarterEquipmentTests/EquipmentCommandCases.cs)만 실행했다. 기본 기존 생성/성장 suite와 PG/UE/HTTP 검사는 반복하지 않았다.

- 분리 Release 빌드/집중 실행 종료0·경고/오류0, **7그룹** 통과. 실제 Manifest의7종 ID/버전/슬롯과 광석 정의, 7종×3단계의 **21개 Record 계획**, 정렬/전량·부분 차감·입력 보존, 잘못된 소유/정의/상태/위치/Revision·잠금/만료/상한 거절을 확인했다.
- 기존 Memory 저장소에 실제 계획을 Commit해 비용2/4·전량 광석 tombstone·성공 재전송의 무차감, 같은 광석을 계획한 두 작업의 CAS 승자/패자·패자의 장비/성공 영수증 미생성을 확인했다. 비용6은 Record 계획에서 확인했고 실제 연속3단계/광석12 Commit을 수행한 것은 아니다.
- 두 장비 이전은 대상이 먼저 이동하면 원본 단계도 보존되고, 새 조회 뒤에는 함께 확정됐다. 그 뒤 대상이 다시 이동해도 옛 성공 영수증 재전송은 당시 결과를 반환하며 최신 저장 위치를 덮어쓰지 않았다. HTTP의 영수증 사전 복구 처리자가 구현됐다는 뜻은 아니다.
- 입력108개 전후 해시 동일. 첫 실행은 재료 Manifest의 보상 항목에도 아이템 ID가 있다고 가정한 사례 코드에서 실패했다. 사례 선택만 `TryGetProperty`로 수정하고 runner stderr/종료코드 보존을 보완했다. 제품 domain DLL 해시는 두 빌드에서 동일했다.

[ImplementationResult.json](../Saved/BuildChecks/ProgressionEquipmentCommands-20261031/ImplementationResult.json)과 명령/입력·모듈 해시·Build/Cases 로그가 근거다. 두 시도 자체 artifacts/DLL/obj **10,751,660 bytes(10.25MiB)** 폐기·Temp 잔여/예외0이다. 검증 DLL은 폐기해 해시는 당시 증거로만 남긴다. 훈련 UI 담당의 UE Source/공유 빌드·정상 DLL/Content 입력과 기존 사용자 실행을 유지했고 본인 backend/패키지 읽기·수정 예약은 해제했다. 다음은 최신 예약에 맞춘 인증된 로비 처리자와 리스/아이템 저장의 원자적 연결·영수증 사전 복구다. 원본 에셋 설치/시작 지급·UE/GAS는 각각 별도 인계가 필요하다.

## 14. 2026-11-01 플레이어 API와 리스·아이템 원자 저장

[EquipmentProgressionApiEndpoints.cs](../Backend/ProjectRpg.Backend/Api/EquipmentProgressionApiEndpoints.cs)에 두 경로를 연결했다.

- `POST /api/characters/{characterId}/equipment/upgrade`
- `POST /api/characters/{characterId}/equipment/transfer`

기존 Bearer 인증을 거친 Player만 호출한다. 저장소가 현재 캐릭터의 Steam 소유를 다시 확인하며 관리자/게임 서버 principal이나 다른 캐릭터를 지정하는 것으로 승인을 우회하지 않는다. 클라이언트는 ID·규칙 버전·예상 Revision/단계만 지정한다. Revision은 JSON 문자열이고 단계는 필수 값이며 가격·변경 Record·owner·fingerprint는 서버가 결정한다. UTF-8 JSON 본문은16KiB로 제한한다. 요청 예제/오류 계약은 [Item API의 새 절](../Backend/ITEM_API_V2.md#로비-장비-강화이전--2026-11-01)을 따른다. 일반 Commit의 게임 서버 전용 권한은 유지했다.

[IEquipmentProgressionRepository](../Backend/ProjectRpg.Backend/Data/IEquipmentProgressionRepository.cs)를 기존 게임 저장소에 연결했다. 캐릭터 소유를 확인한 뒤 서버가 전체 명령에서 계산한 fingerprint·operation·actor가 일치하는 성공 영수증을 **리스/현재 단계·위치/재료 조회보다 먼저** 반환한다. 새 요청이면 유효 리스를 검사하고 서버 Record 조회→기존 계획→장비/비용·성공 영수증 저장을 같은 경계에서 수행한다. 실패 응답에는 다른 요청/다른 캐릭터의 영수증 본문을 포함하지 않는다. 반환 배열은 복사해 호출자가 저장 영수증을 변경하지 못하게 했다.

Memory는 기존 pickup의 `dungeon→item gate`와 생성의 `item→character` 순서를 유지해 `dungeon→item gate→character` 안에서 처리한다. 입장·다른 아이템 쓰기가 검사/확정 사이에 끼어들 수 없다. 공유 아이템 저장소 없는 독립 게임 저장소는 이 명령을 실행하지 않는다.

PostgreSQL은 캐릭터의 `FOR UPDATE`로 입장의 FK `KEY SHARE`와 직렬화하고, 같은 연결/트랜잭션에서 기존 item owner/request 잠금·성공 조회·리스 행 잠금·본인 아이템만의 정렬 조회·기존 `CommitInTransactionAsync`를 사용한다. 신규 리스 삽입은 캐릭터 FK 잠금으로, 기존/만료 리스의 갱신은 행 잠금으로 보호한다. 기존 입장 정리가 리스 행을 먼저 지운 뒤 캐릭터 FK를 요청할 수 있어, 리스 행은 **`FOR UPDATE NOWAIT`**로 얻는다. 사용 중이면 `equipment_lease_busy`/409와 전체 롤백으로 끝내며 캐릭터 잠금을 붙잡고 반대 순서의 정리를 기다리지 않는다. 클라이언트는 같은 명령으로 재시도할 수 있고 성공 영수증이 이미 있으면 이 busy 검사도 거치지 않는다.

기존 v2 nullable 단계/영수증 테이블을 재사용하며 새 DB 마이그레이션은 없다. PG 게임 저장소의 새 명령은 주입된 아이템 저장소가 필요하고 실제 Program DI가 제공한다. 기존 서비스/DB를 재시작·변경하지 않았다. 기존 시작 대검/빈 단계를 새 장비로 자동 전환하지 않으며 원본 에셋/새 시작 지급·UE/UI/GAS는 별도다.

검사는 기존 StarterEquipmentTests의 `--equipment-lobby-memory`와 `--equipment-lobby-postgres`로 한정했다.

- 분리 Release 빌드 성공·경고/오류0. HTTP/Memory **7그룹**: 실제 session Bearer middleware/Kestrel·무인증401/관리자·다른 캐릭터403/플레이어 일반 Commit403, 강화/이전 저장·리스 중 성공 재전송, 필수 값/Int64 문자열/JSON·본문 제한, Memory 소유·반환 사본·리스/경합의 부분 쓰기 방지를 확인했다. 합성 계정/일회성 Memory이며 Steam 플랫폼 인증·운영 호스트 시험은 아니다.
- 최초 전용 PG6그룹 통과 뒤 리스 정리 잠금 역전 가능성을 코드에서 확인해 NOWAIT와 실제 삭제→FK 삽입 경합 사례를 추가했다. **PG만 다시 빌드/실행해 최종7그룹** 통과했다. 실제 입장 생성이 장비 트랜잭션의 캐릭터 잠금과 직렬화되는 것, busy 전체 롤백/정리 진행, 단계/비용·영수증 원자성·새 연결 풀의 성공 재조회를 확인했다.
- 두 성공 단계의 입력은 각각114개 불변이다. 그 사이 변경은 PG 장비 partial과 해당 PG 사례 파일2개뿐이며 API/Memory/HTTP 사례 소스는 같아 HTTP/Memory를 반복하지 않았다. 두 단계의 서로 다른 모듈 해시를 각각 기록했다. 첫 빌드의 HTTP 사례 `Require` 이름 해석 오류는 명시적 helper로 수정했고 성공 결과와 구분한다.

근거는 [ImplementationResult.json](../Saved/BuildChecks/ProgressionEquipmentLobby-20261101/ImplementationResult.json), 각 단계의 Result/입력 해시·Build/MemoryHttp/Postgres 로그다. 초기 실패와 두 성공의 자체 artifacts/DLL/obj **16,194,043 bytes(15.44MiB)** 및 소유 라벨의 PG tmpfs 컨테이너2개를 폐기했다. tmpfs 삭제 용량은 별도 계측하지 않았으며 Temp/컨테이너 잔여·정리 예외0이다. 작은 명령/해시/결과만 남기고 폐기 모듈은 당시 증거로 구분한다. UE/공유 빌드·정상 DLL/다른 프로세스·포트/DB와 타 담당 파일을 변경하지 않았고 본인 예약을 해제했다. 다음은 최신 Content 독자의 인계를 확인한 재료/장비 설치·명시적 지급과 UE 요청/미확정 복구·GAS 연결이다.

## 15. 2026-11-01 장비 패키지의 누락 전용 설치기

[InstallProgressionEquipmentPackage.ps1](../Scripts/Editor/InstallProgressionEquipmentPackage.ps1)를 추가했다. 기본은 파일을 복사하지 않는 Plan이며, `-Apply`에서만 장비7개·태그 INI1개의 누락 파일을 복사한다. 기본 대상은 원본 프로젝트, 패키지는 `ContentAuthoring/ProgressionEquipment`다. 기존 재료 설치기와 같은 누락 전용 정책을 사용하며 두 설치기를 서로 대신 실행하지 않는다.

```powershell
# 현재 누락/동일/충돌 조회. 작은 보고서만 Saved 아래 기록한다.
Scripts/Editor/InstallProgressionEquipmentPackage.ps1

# 활성 Content/Config 입력 사용 종료·재예약 후 원본에 적용할 명령.
Scripts/Editor/InstallProgressionEquipmentPackage.ps1 -Apply
```

패키지 버전·정확한8개 상대 경로·7종 모델/ID/DefinitionVersion1/슬롯·registry ID 선언·파일 해시와 대상의 기존 대검 template 해시를 **복사 전에 전부 확인**한다. 기존 대상은 같으면 바이트/수정시각을 보존하고 다르면 쓰기 전에 거절한다. manifest 경로 이탈·패키지 변조·대상/패키지/보고서 경로의 reparse point와 프로젝트 루트 위의 junction도 거절한다. 드라이브 루트를 `C:` 같은 상대 경로로 바꾸어 쓰지 않게 차단한다. 보고서는 Saved 안이면서 원본 패키지 밖이어야 하므로 패키지가 Saved 아래 있을 때도 Manifest를 덮지 않는다.

실제 복사는 `overwrite=false`이며 파일별 완료를 보고서에 남긴다. 복사 도중 I/O 실패가 나면 앞서 완료한 새 파일을 자동 삭제하지 않는다. 재개 시 보고서와 현재 대상을 대조해 동일 파일은 보존하고 나머지만 적용한다. 이 작업은 파일 배치이며 캐릭터 지급·강화 단계 초기화·장착·GAS/DB 변경을 수행하지 않는다. native 재로드는 원본 적용 뒤 별도 단계다.

[원본 Plan](../Saved/BuildChecks/ProgressionEquipmentInstall-20261101/Original-Plan.json)은 **누락8/복사0**, 적용 대상은 총22,400 bytes다. 실제 복사는 자기 작은 프로젝트 fixture에서8개, 반복0개/동일8개 보존·수정시각 동일이었다. 원본/fixture Plan·실제 복사/반복·다른 기존 대상·변조 checksum·junction·manifest 이탈·잘못된 ID/슬롯 선언·template 변경·보고서의 원본 덮기·상위 junction·드라이브 루트 거절의 **최종13사례**가 기대한 종료코드로 통과했다. 최종 패키지9파일/원본 template 해시는 같고 원본8대상은 여전히 없다.

착수 때 knockback이 Config/공유 UE 입력을, departure가 backend 입력을 고정했다. 마무리 재조회에서 두 담당의 완료/해제를 확인했지만 새 `hud-vitals-asset-20261101`가 원본 HUD를 작성 중이며, 실행 중인 사설 에디터의 Content가 원본 `Content`로 향하는 junction임을 읽기로 확인했다. 따라서 원본의 새 정의/태그를 추가하지 않았고 그 에디터/파일/예약은 유지했다. 설치 재개는 이 담당과 그때 새로 생긴 Content/Config 독자 전체의 종료/인계를 확인한 뒤 별도 예약한다. 시작 지급도 그때의 backend 담당 범위를 재조회한다.

근거는 [ImplementationResult.json](../Saved/BuildChecks/ProgressionEquipmentInstall-20261101/ImplementationResult.json), 설치기/패키지 해시·각 작은 사례 보고서다. 자기 fixture·복사 패키지/대검·junction·중복 Console 로그 **684,528 bytes(668.48KiB)**를 폐기했고 Temp 잔여/예외0이다. 검사 후 자기 실행기의 preexisting Temp 거절 시 Console 정리에도 소유 플래그를 적용하고 구문만 확인했으며 제품 설치기/사례는 바꾸지 않았다. UE/backend 빌드·실행/DB/기존 지급·원본 Content/Config 변경은 없고 본인 예약을 해제했다.

## 16. 2026-11-01 신규 시작3종 지급 후보 — 원본 미적용

새 캐릭터에 시작 대검/상의/장갑을 명시적 +0으로 함께 생성하는 **후보6파일**을 [Candidate](../Saved/BuildChecks/ProgressionStarterCandidate-20261101/Candidate/Backend/ProjectRpg.Backend/Domain/FirstSliceStarterEquipment.cs)와 [패치](../Saved/BuildChecks/ProgressionStarterCandidate-20261101/StarterGrant.patch)로 준비했다. 원본 패키지 설치는 HUD의 Content 입력과 겹쳤고, 메뉴 담당은 전체 UE Source/공유 빌드, dungeon-query 담당은 backend 본체 입력을 고정했다. 허용된 읽기 사본으로 작업을 분리했으며 **원본 backend/UE·정상 DLL·현재 캐릭터 생성/지급은 변경하지 않았다.**

후보 `FirstSliceStarterEquipment.Create`는 기존 정확한 서버 정의 목록에서 StarterGreatSword/Chest/Gloves만 선택해 Equipment 슬롯9/1/4에 생성한다. 각 아이템은 서로 다른 ID·Version1·수량1·Revision1·Active/CharacterBound·CharacterCreation이며 `FirstSlice/RuleVersion1/Level0`을 명시한다. 임의 수치/태그·광석/증표·목표 장비를 지급하지 않는다. 기존 `StarterEquipment`의 옛 정의/생성 helper는 유지하며 기존 Record/캐릭터를 자동 이전하거나 빈 단계를 +0으로 채우지 않는다.

Memory 후보는 기존 `item gate→character gate` 안에서 새 `SeedStarters`를 호출한다. 전체3개 Record의 형식/ID/슬롯 중복과 기존 저장 충돌을 모두 확인한 뒤 게시해 두 번째 항목의 거절이 첫 번째 장비를 남기지 않는다. PostgreSQL 후보는 기존 캐릭터 INSERT→아이템 INSERT→성장 초기화의 **같은 트랜잭션**에 아이템3개를 넣는다. 새 마이그레이션·런타임 옵션·기존 아이템 변경은 추가하지 않았다. 정상 실행 구성의 시작 지급을 교체하는 후보이며 별도 보상 지급 API가 아니다.

집중 확인은 Source114개를 복사 전후 해시가 같은 사본으로 고정한 뒤, 후보6개·Manifest를 반영한 자기 Input117개에서만 수행했다. 원본은 그 사본 생성 뒤에도 다른 담당이 변경할 수 있으며 이번 최종 감사에서는 Source/Base 대상 변경이 없었다.

- 분리 Release 빌드 종료0·경고/오류0. 실제 작성 Manifest와3종 정의/버전/슬롯·각 +0/고유 ID를 대조했다.
- Memory **4그룹**: 신규3종 장착/초기 레벨1·XP/SP0 유지, 중복 이름의 추가 지급 방지, 새 형제 캐릭터 생성 시 기존 옛 대검/미설정 단계 보존, 배치의 두 번째 ID/슬롯 충돌에서 첫 장비 미생성을 확인했다.
- 기존 StarterEquipmentTests 기본 생성/성장 검사도 종료0이다. 기대 시작 지급만3종으로 바꾸고 기존 정산·해금/SP·편성/권한·동시 Revision 사례를 실행했다. 전체 다른 backend suite나 UE 게임 검사를 수행한 것은 아니다.
- 전용 PG **5그룹**: 실제 신규/중복·기존 아이템 보존, 두 번째 상의 INSERT를 거절하는 자기 trigger에서 캐릭터/앞선 대검 롤백·성장 기록 미생성, 같은 이름 재시도 성공·새 연결 풀의3개 +0 재조회를 확인했다. 첫 PG 실행은 자기 fixture 계정 ID가 기존 VARCHAR(20)을 넘은 실패였고 fixture만 짧게 바꿨다. 스키마/제품 규칙을 바꾸지 않았다.
- 후보6개 해시는 검사 뒤 같고 패치의 LF/new-file 표기를 정정한 뒤 `git apply --check --whitespace=error-all` 종료0을 확인했다. 이 명령은 적용하지 않는 대조이며 원본 적용/쓰기 예약을 뜻하지 않는다. 패치 형식 보완으로 컴파일/사례를 반복하지 않았다.

적용 전에는 세 경계를 순서대로 확인한다. 우선 최신 Content/Config 독자 종료/인계 후 실제 장비 정의를 설치·재로드한다. 다음 backend 고정 입력 사용이 끝나면 원본6경로의 BaseHashes/문맥을 다시 대조하고 별도 쓰기/컴파일 범위를 예약한다. 마지막으로 [RPGDungeonFlowE2EProbe.cpp](../Source/Project_RPG/Private/Tests/RPGDungeonFlowE2EProbe.cpp)의 `2 + optional helm` 아이템 수·옛 `GameItem.Equipment.Weapon.GreatSword` 조회 두 곳을 해당 UE 검사 담당과 인계한다. 이 가정은 새 캐릭터3종과 맞지 않으며 원본 프로브를 이번 후보에서 수정하지 않았다. 패키지/새 지급·UE 복원/실제 전투의 완료를 후보 검사만으로 선언하지 않는다.

근거는 [ImplementationResult.json](../Saved/BuildChecks/ProgressionStarterCandidate-20261101/ImplementationResult.json), Source/Base/Candidate·Input 해시와 Build/Memory/PG·기존 기본 검사 로그다. 첫 실패와 최종 성공의 자체 Input/Artifacts/DLL/obj **13,837,461 bytes(13.20MiB)**·소유 PG tmpfs 컨테이너2개를 폐기해 Temp/컨테이너 잔여·예외0이다(tmpfs 용량 별도 미계측). 후보6파일·patch·작은 증거만 유지하고 폐기 모듈 해시는 당시 사본의 증거로만 남긴다. 본인 후보/검사/문서 예약은 해제했고 원본 적용 예약은 없다. 기존 서비스/DB·원본 에셋/설정·타 담당 파일/프로세스/포트는 변경하지 않았다.

## 17. 2026-11-01 증표 선택 제작과 목표 장비 분해

[FirstSliceEquipmentCraftingPlanner.cs](../Backend/ProjectRpg.Backend/Domain/FirstSliceEquipmentCraftingPlanner.cs)와 기존 장비 API/저장 경계를 연결했다. **실제 backend 소스12파일에 구현했으며 현재 서비스 배포·UE 요청/UI·원본 장비 설치는 하지 않았다.** 정확한 HTTP 형식은 [아이템 API 계약](../Backend/ITEM_API_V2.md#로비-장비-제작분해--2026-11-01)을 따른다.

| 명령 | 서버가 검증할 입력 | 한 번의 원자 저장 결과 |
|---|---|---|
| `equipment/craft` | 규칙1·목표4종 중 정확한 ID/Version1·가방의 본인 증표1~15스택/Revision·합계8 이상 | 증표8 차감/0 스택 Consumed, 서버 생성 ID의 목표 장비1개/CharacterBound/명시적 +0 |
| `equipment/dismantle` | 규칙1·목표4종/Version1·본인 가방 또는 정확한 장착 슬롯·Revision/현재 +0 | 장비 전체 Consumed, CharacterBound 증표1개 지급 |

제작 대상은 인내/순환 대검·목표 상의/장갑뿐이다. 시작 장비·옛 정의·단계 미설정·강화 장비는 분해하지 않는다. 입력은 Active/CharacterBound·잠금 없음·미만료가 필요하며 메일/창고에서 비용을 가져오지 않는다. 장비의 단계 검사는 기존 강화/이전 검증을 내부 재사용하며 이전 명령 동작은 유지했다.

가방 지급은 기존 `DungeonRewardDeliveryPlanner`의 같은 구현으로 처리하고 기존 DungeonReward 진입 검사는 유지했다. 비용 소비 뒤의 가방 스냅샷에서 첫 빈 칸(80칸)을 고르며 남는 칸이 없으면 메일에 생성한다. 분해 증표는 호환되는 스택에 최대999까지 합친다. 메일 결과도 같은 성공 영수증에 포함한다. 클라이언트는 가격·결과 ID/Record를 지정하지 않는다.

새 명령도 **소유 확인→정확한 과거 성공 복구→새 변경의 리스/현재 Record 검사→전체 Commit** 순서다. 결과 GUID 생성은 영수증 복구 뒤의 계획 안에서만 수행한다. 비용 스택 순서를 정규화해 같은 집합의 순서 변경은 재전송으로 인정하며, 같은 RequestId의 대상/비용 집합 변경은 거절한다. 현재 결과 장비가 분해되고 던전 리스가 생겨도 옛 성공은 당시 Record/ID 그대로 복구한다. 재전송 영수증을 현재 아이템의 변경으로 다시 쓰지 않는다.

Memory는 기존 dungeon→item→character 경계 안에서 전체 가방을 함께 읽고, PG는 기존 캐릭터/FK·owner/request·리스 NOWAIT 경계에서 본인 비용/장비와 가방 행을 잠가 같은 연결의 Commit을 호출한다. 현재 DB 구조/SQL·일반 Commit 권한을 변경하지 않았다.

최신 조율표의 다른 backend/UE 독자는 짧은 복사와 전후 해시 확인 뒤 닫힌 사본을 실행하는 범위임을 확인했다. 원본 설치는 새 Vitals PIE의 Content 읽기와 겹치므로 계속 미착수이며, 기존 시작3종 후보도 재포장/재검사/적용하지 않았다. 후보의 기존 해시/패치는 역사적 인계 자료다. 이후 적용 시 이번 Starter 테스트 Program 변경을 포함한 새 문맥에 맞춰 다시 대조해야 한다.

검사는 원본 입력121개를 전후 파일 집합/해시가 같은 사본으로 취득해 독립 artifacts/동적 loopback HTTP·소유 라벨의 PG17 tmpfs에서 수행했다. 두 Release 빌드는 종료0·경고/오류0, 사본121개 불변·검사 종료 시 원본 입력 변경0이었다.

- Memory/실제 Bearer HTTP **9그룹**: 네 목표의 제작/분해·옛 성공 복구, 분리 스택과 정렬 재전송, 같은 증표 경합의 단일 성공/패자 무영수증, 소비 칸 재사용/메일, 장착 장비 분해의998→999 병합/999일 때 우편, 부족/다른 소유의 무변경, 잠금·만료·귀속·Revision·창고·옛 장비·시작/+1/미설정 등17개 거절, 실제 Player/소유·일반 Commit 금지와 JSON/Int64 문자열/16KiB 경계를 확인했다. Steam/운영 인증 검사는 아니다.
- 전용 PG **8그룹**: 같은6개 저장 그룹과 영수증 INSERT 강제 실패의 비용/결과/영수증 전체 롤백·동일 요청 재시도, 새 연결 풀의 원래 결과 ID 복구를 확인했다.
- 공유 경계 변경의 영향으로 기존 강화/이전 Memory/HTTP7·PG7그룹을 확인했다. 공유 지급 구현의 기존 보상 Memory25사례도 통과했다. 보상 PG suite나 UE 게임 검사를 실행한 것은 아니다.

첫 빌드는 사례의 `Require`가 최상위 Program 로컬 함수로 해석되는 CS8801로 실패했고, 사례2개의 명시적 helper로 수정했다. 제품 소스는 그 사이 바뀌지 않았다. 성공 뒤 보존 실행기만 외부 `PROJECT_RPG_REWARD_TEST_POSTGRES`를 검사 중 비우고 복구하게 보완해 향후 공유 DB 자동 접근을 막았다. 당시 보상 PG 환경은 비어 있었고 로그에도 건너뛰기를 기록했다. 이 실행기 보완은 구문 검사만 했으며 이미 통과한 제품/사례를 반복 실행하지 않았다.

근거는 [ImplementationResult.json](../Saved/BuildChecks/ProgressionCrafting-20261101/ImplementationResult.json), 입력/소스·모듈 해시와 Build/Crafting/Lobby/RewardDelivery 로그다. 실패/성공 두 시도의 자기 Input/Artifacts/DLL/obj **17,036,723 bytes(16.25MiB)**와 소유 PG tmpfs 컨테이너1개를 자동 폐기했다. tmpfs 용량은 별도 미계측이며 Temp/컨테이너 잔여·정리 예외0이다. 해시는 폐기한 당시 모듈의 증거만 남긴다. 타 담당 소스/에셋·공유 UE 빌드/정상 DLL·서비스/기존 DB·프로세스/포트는 변경하지 않았고 본인 예약을 해제했다. 다음은 최신 Content/Config 인계 후 설치/명시적 지급, UE 요청/복구·UI/GAS, 실제 보상/추첨 연결이다.

## 18. 2026-11-01 정산 보상의 명시적 장비 단계 보존

제작품과 달리 기존 `DungeonItemReward`에는 성장 상태가 없었고, 정산 Worker는 항상 단계가 없는 ItemRecord를 만들었다. 이를 backend4파일과 집중 사례3파일에서 연결했다. `DungeonItemRewardContract`/domain 보상에 선택 `EquipmentProgression`을 추가하고, 실제 `Program` 정산 API의 검사·canonical fingerprint와 저장된 보상 JSON을 거쳐 Worker가 지급 Record에 그대로 복사한다. 새 DB 구조/SQL은 추가하지 않았다. [HTTP 형식과 권한](../Backend/ITEM_API_V2.md#정산-보상의-선택-장비-단계--2026-11-01)을 따른다.

명시 객체에는 GrowthBandId/RuleVersion/Level이 모두 필요하다. `FirstSlice/1/0~3`·정확한 장비7종/Version1·수량1/MaxStackSize1·CharacterBound만 허용한다. **이 범위는 이미 결정된 보상 상태의 전달/보존이며, 첫 보스의 +0 드롭 생성·개인별20% 추첨을 구현한 것은 아니다.** 재료/옛 정의에 성장 상태를 붙이거나 미지원 단계·불완전 객체를 보내면 거절한다. 누락/null은 그대로 미설정이며 기존 보상/캐릭터를 자동 +0으로 만들지 않는다.

null 필드를 직렬화에서 생략해 과거 보상의 canonical JSON/fingerprint를 유지한다. 명시 상태가 있으면 정산 fingerprint와 저장된 snapshot에 포함돼 같은 정산의 단계 변경이 충돌한다. Worker 재시도는 기존 결정적 ItemId·원자 지급/영수증 경로를 사용한다. 같은 정의의 성장 장비를 합치지 않으며 가방/우편 모두 상태를 보존한다. 이후 이동·성장한 ItemRecord에 원래 지급 batch를 재전송해도 최신 상태를 덮어쓰지 않는다.

소유/권한 정책은 유지했다. 실제 HTTP 정산은 배정된 GameServer만 제출하며 Player/관리자는 거절한다. 정산 완료 뒤 서버의 기존 제한 자격으로는 snapshot GET/감사만 허용하고 새 POST는 거절한다. 저장소/Worker의 재전송 보장을 기존 자격의 권한 확대와 혼동하지 않는다.

검사는 backend/사례/SQL/Manifest의 파일 집합과 전후 해시가 같은 **닫힌 입력123개**에서 수행했다. 별도 출력의 Release2빌드 종료0·경고/오류0, 사본 불변·검사 종료 원본 변경0이다. 현재 서비스/DB를 사용하지 않고 자기 동적 loopback backend와 소유 라벨의 PG17 tmpfs만 실행했다.

- Memory/실제 HTTP **7그룹**: 지원 조합28개/잘못된 보상12개, 과거 canonical JSON/fingerprint 동일·명시 단계별 identity 차이, 두 수령인의 가방/우편·서로 다른 성장 장비/옛 미설정 장비 보존, 고정 snapshot 충돌, 실제 지급 성공 뒤 응답 유실의 Worker 재시도·중복 방지 및 이후 +1/이동 보존을 확인했다. 실제 backend `Program`을 자식 프로세스로 실행해 개발 인증/출발/배정/시작→정산→Worker→snapshot GET의 +0 보존과 잘못된 HTTP 객체12개·권한을 확인했다. 복제한 endpoint/Steam 플랫폼 인증/UE 게임 검사는 아니다.
- 전용 PG **4그룹**: 같은 두 수령인·가방/우편·snapshot 충돌/옛 상태 보존·응답 유실 재시도/최신 성장 보존과 새 연결 풀의 정산 JSONB 복원→새 Worker→아이템 성장 재조회를 확인했다.
- 직접 영향의 기존 보상 suite는 **Memory25·PG25사례** 모두 통과했다. 기존 영수증/파티 롤백·가방/우편·경합 동작을 유지했다. 이번에 다른 로비/전투 전체 검사를 반복하지 않았다.

근거는 [ImplementationResult.json](../Saved/BuildChecks/ProgressionRewardGrowth-20261101/ImplementationResult.json), 입력/소스·모듈 해시와 실제 실행 명령/작은 로그다. 자기 backend 자식 종료·파일 사용 해제 뒤 Input/artifacts/DLL/obj **11,031,895 bytes(10.52MiB)**와 소유 PG 컨테이너1개를 자동 폐기했다. tmpfs 용량 별도 미계측, Temp/컨테이너 잔여·정리 예외0이며 폐기 모듈 해시는 당시 증거만 남긴다. 본인 예약을 해제했다.

착수/검사 시 다른 담당의 원본 Content 읽기·공유 UE 빌드와 backend의 닫힌 실행 사본 범위를 확인해 원본 에셋/Config·UE 소스/공유 출력·타 담당 스크립트/프로세스/포트·기존 DB를 변경하지 않았다. 기존 시작3종 후보도 재포장/적용하지 않았다. 이 backend 구현 당시 미연결이던 UE 보상 구조체/정의·전송은 아래19절에서 후속 연결했다. 원본 패키지 설치 상태는 [설치 문서](PLAYER_PROGRESSION_PACKAGE_INSTALLATION.md)를 따른다. 실제 지급/개인별 추첨·시작3종 후보 적용은 서로 다른 후속이다.

## 19. 2026-11-01 BP 서버 보상 작성과 UE 정산 전송 연결

`FRPGDungeonItemRewardEntry`에 기본 false인 `bIncludeEquipmentProgression`과 조건부 편집 단계 `EquipmentProgressionLevel`(0~3)을 추가했다. 서버 보상 DataAsset의 ItemRewards에서 기존 ItemDefinition·수량1·CharacterBound를 지정하고 이 옵션을 켜면 `BuildSettlement`가 `FirstSlice/1/명시 단계`를 만든다. BP에서 작성/조회할 수 있는 기존 보상 정의를 확장했으며 영속 아이템 상태 자체를 편집 가능하게 바꾸지 않았다. 과거 에셋/보상은 옵션이 꺼져 있어 성장 미설정 그대로다. +0도 명시적으로 켠 경우에만 전달한다.

`FRPGDungeonItemReward`의 선택 상태와 공통 검사를 정의 작성·GameMode 저수준 정산 가드·실제 `HttpWebManager` JSON 작성에 연결했다. backend와 같은 정확한 장비7종/DefinitionVersion1·FirstSlice1/0~3·수량/MaxStackSize1·CharacterBound만 허용한다. 잘못된 값은 일부 보상 제출이나 +0 보정 없이 거절한다. 미설정은 `equipmentProgression` 필드를 생략하며 기존 JSON 필드·권한/RPC·가방 우선/우편 정책은 유지한다.

기존 서버 정의 선택→확정 보상 복사본→PendingItemRewards→정산 요청의 값 복사를 그대로 사용한다. 동일 버전 에셋을 나중에 +3으로 편집해도 이미 고정한 +0 스냅샷/JSON이 바뀌지 않는다. 무작위 추첨·수령인별 다른 보상·기존 장비 자동 전환·시작3종 지급 후보·성장 명령/UI/GAS는 이 변경에 포함하지 않았다. 원본 Content/Config/backend/DB/서비스도 변경하지 않았다.

처음에는 다른 담당의 공유 빌드 예약을 피해 새 후보만 작성했고, 정상 설치/집중 검사 및 공유 자원 명시 해제를 확인한 뒤 원본5개 기준 SHA 일치·7파일 예약/재조회 후 적용했다. 원본 정상 UHT/Editor **158작업·lib/DLL 링크4·metadata1·UBT 종료0**, 모듈 `16933734…71550794`다. 기존 전투/로비 코드의 C4996 경고2건은 남아 있으며 오류0/경고0 빌드라고 보고하지 않는다.

빌드 도중 별도 예약된 `RPGDungeonClientFlowTests.cpp`가 바뀌어 **전체 입력1,344개의 불변 감사는 실패로 보존**했다. 본인7은 전후 동일하고 그 변경은 새 보상 검사에서 호출하지 않는 독립 이동 테스트 CPP다. 이를 분리한 `BuildScopeResult.json`만 본인7의 컴파일/링크 근거로 사용하며, 전체 소스 고정·다른 담당의 최신 이동 코드 검증을 주장하거나 불필요한 재빌드를 하지 않았다.

닫힌 실행 사본 입력1,167개와 실제 사설 모듈 로드를 대조해 **native6개·UE 종료0·각 경고/오류0**을 확인했다. 새4개는 지원 조합28개/잘못된 조합의 wire 거절·누락 필드 생략, 보상 작성/실패 출력 초기화, 실제 서버 정의 선택의 고정 스냅샷, UHT/BP 편집 메타데이터와 영속 상태 비편집성을 검사한다. 기존 보상 작성2개만 직접 영향 검사로 실행했다. 기동 로그 Error16/테스트 발견 이후0을 구분하며 전체 Editor 기동 오류 해결로 계산하지 않는다. 작성 사례는 저장된 태그 이름의 단위 fixture이고 원본 에셋/그래프 실행이나 실제 UE→HTTP/PG 지급 E2E가 아니다. backend 실제 HTTP/PG 근거는18절의 별도 결과다.

근거는 [ImplementationResult.json](../Saved/BuildChecks/ProgressionRewardUE-20261101/ImplementationResult.json), `BuildResult.json`/`BuildScopeResult.json`·`OwnCompiledInputs.json`·변경 patch와 `502387ff772b4d20aaf4b9579c2aedd9/Verification.proof.json`이다. 자기 UE 종료/파일 해제 뒤 실행 사본·DLL·캐시188,437,115 bytes와 중복 후보7파일208,931 bytes, 합계 **188,646,046 bytes(179.91MiB)**를 자동 폐기했다. 임시 경로/정리 예외0·작은 명령/해시/로그/결과만 보존하며 정상 Binaries/Intermediate·원본 소스/유지할 테스트는 남긴다. 폐기 사본 해시는 당시 증거만 남는다. 공유 빌드/정상 출력은 사본 취득 직후 해제했고 본인7/문서도 완료 후 해제한다. 다른 담당의 에셋 설치·HUD 작성·이동 코드/프로세스와 예약은 유지했다.

## 20. 2026-11-01 신규 시작3종 원본 적용과 실제 2인 복원

16절의 완료 후보를 재구현하지 않고 현재 원본에 적용했다. 후보 작성 뒤 게임 저장소의 권한 보완과 StarterEquipmentTests의 제작/보상 진입 분기가 추가되어 3개 Base SHA와 기존 패치 문맥이 달랐다. 기존 보안 코드/분기를 보존하고 시작 지급 변경 부분만 `InMemoryGameRepository`, `InMemoryItemRepository`, `PostgresGameRepository`, 신규 `FirstSliceStarterEquipment`·기존 테스트 Program/사례6파일에 옮겼다. 후보 원본/과거 근거는 유지했다.

신규 생성은 시작 대검·상의·장갑을 슬롯9/1/4에 각 Version1·수량1·Revision1·Active/CharacterBound·CharacterCreation·서로 다른 ID·명시적 FirstSlice/1/+0으로 만든다. Memory는 전체 배치 사전 검사 후 게시하고 PostgreSQL은 캐릭터/아이템3개/성장 초기화를 같은 트랜잭션에 넣는다. 옛 `StarterEquipment` helper와 기존 캐릭터/아이템은 유지하며 빈 단계를 +0으로 보정하지 않는다. 새 SQL/마이그레이션·운영 배포/기존 서비스 교체는 없다.

선행 설치14파일은 기존 작성기2개의 read-only/reload를 닫힌 사본에서 실행했다. 모듈 `16933734…71550794`의 실제 로드·아이템9종의 등록 ID/정의·태그/슬롯·보상3개 참조·native 검증, 저장0/dirty 없음·원본14 SHA 동일·UE0이다. HUD/GA의 활성 원본은 전후 cohort가 같은 overlay 사본만 사용했고 에셋을 새로 저장하지 않았다. `fa95e6a7f7c44aa0a1c14bc2560ad1fa/Verification.proof.json`·Materials/Equipment 보고서가 근거다.

현재 원본의 닫힌 입력122개(후보 overlay0)에서 별도 Release 빌드 종료0·경고/오류0, Memory4·소유 PG5그룹과 기존 기본 생성/성장 suite 종료0을 확인했다. 신규3종/중복 이름·두 번째 ID/슬롯 충돌의 전체 사전 거절, 두 번째 장비 INSERT 실패에서 캐릭터/앞선 장비·성장 기록 전체 롤백·같은 이름 재시도/새 연결 풀의 +0 재조회, 새 형제 생성 때 기존 옛 대검/성장 미설정 보존을 확인했다. 기존 사례의 `candidateOnly`/scope 문자열은 재사용한 fixture의 과거 이름이며, 실제 입력 분류는 wrapper의 `currentOriginalSource=true`/`candidateOverlay=false`/`originalApplied=true`와 입력122 SHA다. 실행 뒤 현재 backend 입력 변경0이다.

실제 호출 소비자도 인계했다. `RPGDungeonFlowE2EProbe.cpp`는 옛1종과 신규3종을 구분해 정확한 슬롯/명시적 +0·버전/귀속을 관측하고 저장 무기의 두 ID를 읽는다. 서버/RPC/게임 판정은 바꾸지 않았다. 정상 Editor6작업(CPP3 중 본인 프로브1·타 담당 완료 이동2 포함)/링크2/metadata1·종료0·입력1,344 불변, 모듈 `B6929AF1…450BCB28`이다. 다른 담당의 사설 UE2개를 종료하지 않았으며 원본 DLL 매핑0을 확인하고 순차 빌드했다. 이동2의 검사를 새로 완료했다고 주장하지 않는다.

실제 실행은 첫 작성기 사본 누락, 다음 `NoBackendRestore` 혼용의 준비 실패를 보완했다. 세 번째는 **이미 두 클라이언트의 신규3종 복원/보상/로비 복귀에 도달했지만** PS 최종 검사의 옛 대검1종 기대 때문에 실행 종료1이었다. 이를 성공으로 승격하지 않고 보존했다. 해당 `Run-DungeonBossReturnE2ETest.ps1`의 신규 생성 검사/포기 후 보존만 현재3종으로 바꾸고 기존 전체 Record 비교는 ID 정렬 후 유지했다. 실제 helper를 추출한 순수25사례(0임시/게임 실행0)에서 잘못된 ID/소유·슬롯·대소문자·귀속/잠금·단계·중복·옛1종 대체를 거절했다. 게임 CPP/DLL은 다시 변경/빌드하지 않았다.

최종 `5c0a809602a54443aeeb55f9a23956a6/E2E-0256d1e62e654e4cac6b1ce3dffaf904`는 **runner 종료0/Passed=true**다. 실제 사설 B692 모듈3개(서버/클라이언트2) 로드, 인증된 UE 로비·파티/배정/티켓→Iris 서버 복원→스크립트 보스 처치→서버 정의 Normal 정산→각 소유 가방·저장 데이터 검사→두 결과 버튼/실제 로비 복귀를 완료했다. 각 클라이언트에서 `Family=FirstSlice Equipped=3 ExplicitZero=1 LegacyMigrated=0`을 확인했고 HTTP/PG 최종 검사는 3종의 ID/소유·컨테이너/슬롯·Version1/Revision1·Active/CharacterBound/CharacterCreation·명시적 +0과 별도 재화250/회복약2를 검사했다. 파티/개인별 권한과 입장 우회0을 유지했다. 실제 Steam/Shipping·물리 입력·자연 전투·성장 명령/UI/GAS·개인별 드롭 추첨의 완료는 아니다.

근거는 [ImplementationResult.json](../Saved/BuildChecks/ProgressionStarterApply-20261101/ImplementationResult.json), `BackendResult.json`/`BuildResult.json`/`ScriptObservation.proof.json`, 실패3회와 최종 근거다. 자기 UE/backend/파일 사용을 닫은 뒤 native 사본206,069,824 + backend7,324,640 + 실제 연결 네 시도300,195,548, 합계 **513,590,012 bytes(489.80MiB)**와 소유 PG tmpfs 컨테이너1개를 자동 폐기했다. tmpfs 용량은 별도 미계측이다. 임시 경로/정리 예외0·원본 에셋/Config/정상 출력과 기존 DB/캐릭터·타 담당 작업은 유지했다. 작은 로그/명령·소스/모듈 해시만 남기며 JoinTicket 원문/마스킹 해시와 폐기 사본의 당시 증거를 구분한다. 본인 backend6/프로브1/PS1/집중검사1·공유 빌드/정상 출력·사본/3078/18878/문서 예약을 해제한다.

## 21. 플레이어 BP 성장 명령과 미확정 요청 복구

`URPGEquipmentMutationSubsystem`은 GameInstance 단위의 플레이어 전용 접점이다. 기존 좁은 Player POST4를 사용하며 범용 Commit/서버 영수증 조회 권한은 늘리지 않는다. Blueprint에는 전체 변경 Record·가격·결과 아이템 ID·토큰·endpoint를 입력받는 노드가 없다. 서버가 최종 소유/리스/가격/현재 상태·Revision과 원자 저장을 결정한다.

BP 사용 순서는 다음과 같다.

1. 로그인/선택 캐릭터가 확정되면 GameInstance의 `RPGEquipmentMutationSubsystem`을 얻어 `OpenCharacter(CharacterId)`와 명시적인 `RefreshItems(CharacterId)`를 호출한다. `OnEquipmentMutationsChanged`에서는 현재 선택 캐릭터의 `GetState`를 다시 읽는다. `ItemsLoadState=Ready`의 읽기 전용 `Items`/`MailItems`에서 현재 ID·Revision·단계를 얻는다. 변경 이벤트에는 이전 계정의 결과 payload를 싣지 않는다.
2. `UpgradeEquipment`, `TransferEquipment`, `CraftEquipment`, `DismantleEquipment`에는 현재 조회한 ItemId·Int64 ExpectedRevision·ExpectedLevel과 재료 후보만 준다. 제작은 정확한 목표4종만 받는다. true는 **원래 요청을 안전하게 보존했다**는 뜻이지 성공/비용 차감의 확정이 아니다.
3. `AwaitingServer` 동안 중복 명령은 보내지 않는다. `RecoveryRequired`는 원래 입력으로 `RetryPending`만 호출하고, `AuthenticationRequired`는 같은 계정으로 재인증한 뒤 원래 요청을 복구한다. 자동 Tick 재시도나 새 RequestId로 치환하는 취소/강제 초기화 노드는 없다.
4. `Committed`/`Rejected`와 `bNeedsInventoryRefresh`를 받은 화면은 `RefreshItems`로 최신 가방/장비/우편을 새로 읽는다. 이는 기존 `/api/items`의 Player bearer·현재 캐릭터 소유 조회이며 `includeTerminal=false`/limit500·캐시 우회를 지정한다. 기존 `FRPGInventoryProjectionMapper`를 재사용하고 실제 읽기가 끝난 뒤에만 refresh 표시를 해제한다. 현재 API에 페이지 cursor가 없어 결과500개 이상이면 완전한 목록으로 표시하지 않고 Failed로 남긴다. 성공 영수증은 과거 snapshot일 수 있으므로 응답 Record를 현재 투영/GAS에 직접 덮어쓰지 않는다. 서버에서 최신 아이템이 다음 입장/복원 경로에 사용된다.

`Saved/EquipmentMutations/v1`의 파일은 endpoint·Steam 계정·캐릭터 digest 범위다. 자격 증명/원시 URL 없이 원래 RequestId·정규 본문·fingerprint를 기록하며, Windows 배타 파일 공유/같은 디렉터리 원자 이동/전체 비교 후 삭제로 협력 프로세스의 덮어쓰기를 막는다. 손상·알 수 없는 버전·잠금·쓰기 실패 시 새 명령을 보내지 않는다. 다른 플랫폼의 동일 잠금은 아직 검증하지 않아 의도적으로 unavailable이다. 로컬 journal은 권한 근거가 아니며 서버의 검증을 우회하지 않는다.

401/403·리스 사용 중·404/알 수 없는 거절·전송 실패/5xx/잘못된 성공 본문은 미확정 원본을 유지한다. 현재 계정/endpoint/자격 문맥이 일치하는 정확한 성공 영수증 또는 receipt-first 이후 원자 잠금 안에서 판정한 고정 입력의 확정 거절만 원본과 비교해 정리한다. 응답 도착 전 계정/endpoint가 바뀌면 새 계정의 상태나 아이템을 바꾸지 않는다. UI 알림 중 동기적인 화면 종료/로그인 전환도 실제 전송 직전에 재검사한다.

2026-11-02 위 연결의 구현·정상 반영·검사를 완료했다. 실제 C# planner/mapper 기준5종에서 JSON·SHA256과 2^53 초과 Revision을 대조했고, native5(`BlueprintSurface`, `Codec`, `Journal`, `Recovery`, `Transport`)는 무경고 Success다. 원자 보관/배타 잠금·손상/버전·중복 클릭·동일 ID/본문 복구·계정/endpoint/자격 변경·불명확한 응답/확정 거절과 BP 노출을 확인했다. 최초 UHT/154작업과 조회 연결135작업, 최종 테스트 CPP1/링크2/metadata1·종료0/입력1361 불변을 구분한다. 실제 로드 모듈은 `235068FD…ABBD6B10`이다.

실제 Player bearer/기존 middleware·장비 API·Memory 저장소를 사용하는 독립 호스트의 HTTP4+소유 GET4도 통과했다. 강화 커밋 뒤 첫 응답을 끊고, 서버 아이템을 다음 Revision/슬롯으로 이동·유효 리스를 만든 상태에서 새 Subsystem이 원래 journal의 같은 RequestId/본문을 재전송했다. `Committed→AlreadyCommitted`·광석20→18 한 번/Revision2, 현재 장비 Revision3/슬롯30/+1·최신 읽기 유지와 제작/이전/분해의 성공을 UE 및 별도 서버 proof로 대조했다. 이는 실제 HTTP와 Subsystem 재생성이며 전체 UE 프로세스 재시작·Steam/PG·물리 BP 버튼 시험으로 확대하지 않는다. 최종 사본227개 불변/실제 동일 DLL 로드·UE/fixture 종료0, 계획한 연결 차단의 curl56 경고8만 정확히 분류하고 예상외 검사 경고/오류0이다. 기동 Error15/검사 발견 이후0을 별도 보존했다.

실패도 유지했다. Windows 엔진의 미구현 플랫폼 SHA256 assert는 기존 OpenSSL SHA256으로 교체했고, 중간 EVP 헤더의 UE `UI` 이름 충돌 컴파일 실패와 중복 비용 fixture의 배열 자체 참조 assert를 각각 보완했다. 첫 기능 검사6은 모두 Success였지만 계획 경고8 때문에 엄격 러너 실패였으며 성공으로 바꿔 기록하지 않았다. 검증 호스트의 상속 Kestrel3000은 loopback:0으로 보완했고 설정 초기화의 기동 실패·다른 담당 HUD4 변경의 전체 Source 사본 거절도 보존했다. 최종은 제품 변경/기존 native5 반복 없이 실제 필요한 Source12와 같은 모듈만 복사해 HTTP1을 마쳤다. 따라서 다른 담당의 현재 HUD/개인 추첨·전체 최신 프로젝트 검증은 아니다. 빌드 중 타 담당 신규 DamageTests.cpp 추가에 따른 앞선 전체 파일집합 감사 실패와 후속 정상1361 성공도 구분한다.

근거는 [ImplementationResult.json](../Saved/BuildChecks/EquipmentClientCommands-20261101/ImplementationResult.json), `BuildScopeResult.json`, native5의 `e12b7811f19d4f4e9421044182830a91`, 최종 HTTP의 `bc53027ab6104874a83ab2edeeabda74/HttpProof.json`이다. 모든 실패/준비/최종 runtime·입력·DLL/캐시·검증 자격과 벡터 생성 임시를 합쳐 **926,518,493 bytes(883.60MiB)**, 소유 디렉터리 링크60개를 폐기했다. 크래시 뒤 일시 DLL 공유 예외2회는 파일 사용 해제 후 재정리했고 최초 실패 proof와 DeferredCleanup을 함께 유지한다. 최종 잔여/정리 예외0·정상 출력/원본 에셋/Config/backend 제품·기존 DB/복구 백업·타 담당 프로세스는 유지했다. 폐기 사본의 해시는 당시 증거이며 재사용 artifact가 아니다. 본인 Source12/문서/공유 빌드·읽기·실행/임시 예약을 해제한다. 새 화면·추첨·GAS 확장, 운영 Steam/다중 PC·Shipping/배포는 이 완료 범위 밖이다.
