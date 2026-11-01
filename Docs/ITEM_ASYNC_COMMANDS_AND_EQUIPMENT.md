# Item Async Commands and Authoritative Equipment

## 구현 결과

Dedicated Server의 장착, 장착 해제, 소모 요청을 다음 단일 흐름으로 연결했다.

```text
Owning Client RPC
  -> 서버에서 Character Owner/Container 확정
  -> FRPGItemActionCommandPlanner 검증
  -> 소모 효과 Spec/원래 Pawn·ASC 준비와 서버 실행 범위 고정
  -> Item V2 비동기 Commit
  -> receipt 요청 ID/명령/지문/주체 검증
  -> authoritative Inventory Projection 병합
  -> 장비 Snapshot 기반 Actor/AbilitySet 조정
  -> 검증된 원래 소모 요청의 GameplayEffect 1회 전달
  -> owning client에 결과 통지
```

클라이언트는 `RequestId`, `ItemId`, `ExpectedRevision`과 명령에 필요한 슬롯만
보낸다. Owner ID와 Inventory/Equipment Container ID는 인증된 PlayerState의
Character ID로 서버가 생성한다. 따라서 변조된 RPC로 다른 캐릭터나 컨테이너를
지정할 수 없다.

## 책임 분리

- `FRPGItemActionCommandPlanner`: 읽기 전용 Record source와 정책 catalog만 사용해
  Commit request를 만든다.
- `IRPGItemAsyncCommitter`: 비동기 저장 경계를 추상화한다.
- `FRPGItemAsyncCommandOrchestrator`: 계획, Commit, receipt 검증, 후속 sink 순서만
  조정한다.
- `URPGInventoryProjectionComponent`: 소유자 전용 authoritative read model을
  유지하고 변경을 알린다.
- `URPGAuthoritativeEquipmentComponent`: Equipment record snapshot을 GAS와 월드
  Actor 상태로 멱등 변환한다.
- `URPGItemCommandComponent`: PlayerController의 owner RPC와 런타임 어댑터만
  담당한다.

이 분리로 MMORPG에서는 HTTP/Postgres backend를 사용하고, 소규모 ARPG에서는
같은 Planner와 Reconciler에 인메모리 또는 로컬 Save committer를 연결할 수 있다.

## 멱등성과 순서 보장

- backend의 Request ID와 명령 fingerprint receipt가 영속 멱등성의 기준이다.
- 오케스트레이터는 최근 완료 요청 256개를 프로세스 메모리에 보관해 즉시 재시도를
  backend 호출 없이 `AlreadyApplied`로 처리한다.
- 같은 Request ID를 다른 fingerprint로 재사용하면 `IdempotencyConflict`다.
- backend receipt가 요청 ID, 명령, fingerprint 또는 actor와 다르면 Projection과
  효과 적용 전에 protocol error로 거부한다.
- 늦게 도착한 낮은 Revision receipt는 성공한 과거 요청일 수 있으므로 Projection을
  롤백하지 않는 성공 no-op으로 처리한다.
- 운영 소모 adapter는 전송 전에 원래 효과/대상을 준비하고 서버 생성 실행 범위를 지문에
  붙인다. 같은 파이프라인의 원래 명령은 `Succeeded` 또는 검증된 `AlreadyApplied`로
  **아직 전달하지 않은 효과만** 1회 전달한다. 이미 전달한 효과/완료 캐시는 재실행하지 않는다.
- 이 계약에 참여하지 않는 참조/테스트 sink는 기본적으로 이전의 `Succeeded` 전용 동작을
  유지한다. 일반 `AlreadyApplied` 영수증 자체가 새 효과를 받을 권한은 아니다.
- 2026-10-30부터 원래 성공 영수증이 완전히 검증된 뒤 원래 Pawn/ASC의 사망 또는
  전투 세대 변경이 확인되면 차감은 확정하되 효과는 소멸시키고 기존 복구 잠금을 해제한다.
  이 완료는 회복 GE 실행 성공을 뜻하지 않는다. 아래 당시의 보류 기록보다 이 정책이 우선한다.

현재 소비 복구는 **같은 프로세스·컨트롤러·원래 Pawn/ASC**의 범위다. Pawn 교체 시 옛
효과를 새 대상에 전달하거나 possession 이벤트로 자동 재생하지 않는다. 프로세스 재시작을
넘는 정확히 한 번 실행은 보장하지 않으며, 소비를 가방 디스크 저널 replay 대상으로
확장하지 않았다. 영속 effect receipt만으로 DB와 휘발성 GAS 사이의 원자성이 생기지는
않는다. 전투 상태의 영속/복원·적용 단계·실행 세대와 증거 부족 시 운영 해소 정책이 별도다.

## 세션 내 소비의 원래 요청·효과 복구 (2026-10-27, 구현·집중 native 통과)

`URPGItemCommandComponent`의 실제 adapter를 scoped consume 복구에 연결했다. 클라이언트
RPC/BP 서명·권한·아이템 CAS/API/스키마는 유지하며 기존 owner-only 복구 목록과 공통
ViewModel/복구 버튼을 사용한다. 이전 전체 영수증 mutation 검증 수정도 보존했다.

- 계획 후 HTTP 전에 정의 ID/version·사용량·효과 클래스·유효한 magnitude와 현재 권위
  Pawn/ASC를 확인하고 Spec/기대 영수증을 고정한다. 준비 실패는 `PreparationRejected`이며
  HTTP/수량 감소·가짜 미확정 복구를 만들지 않는다. 효과 정의는 준비된 Spec과 함께 보존한다.
- 실행 범위는 서버가 새 파이프라인마다 만들며 클라이언트가 지정하지 않는다. 새 파이프라인에
  같은 요청 번호/오래된 아이템 상태가 들어와도 이전 범위의 영수증을 새 효과에 사용할 수 없다.
  기존 파이프라인의 재시도는 원래 ID·정확한 int64 revision·전체 본문을 유지한다.
- 응답 유실·영수증 불일치·적용 실패는 원래 계획과 준비된 효과를 메모리에 보존하고 기존
  복구 UI로 수동 재확인한다. 이미 미확정인 요청은 401/404/충돌로 해소하지 않는다. 수령한
  영수증의 주체/범위·수량·전체 레코드는 Projection/효과 전에 다시 검증한다.
- GAS 필터가 거절한 효과는 성공으로 표시하지 않는다. Instant 효과의 invalid active handle을
  실패로 오판하지 않고 `WasSuccessfullyApplied()`를 사용한다. 실행 중 재진입은 거절하며
  성공 뒤 효과 대상은 정리하고 완료 ID는 유지한다. 나중 정의 값 변경은 준비된 Spec에
  소급하지 않는다. 확정 최초 거절·정상 완료는 미전달 효과만 정리하며 DB를 롤백하지 않는다.
- 소유 신원 회수/EndPlay는 로컬 효과 핸들을 정리하고 늦은 응답은 기존 권한 장벽이 막는다.
  원래 Pawn/ASC가 없거나 다른 대상으로 교체됐다면 자동 회복/새 대상 적용 없이 보류한다.
  이 보류를 언제 보상/종료할지는 별도 정책이며 시간 경과로 강제 해제하지 않는다.

새 native 5개는 실제 임시 UE 월드·인증 PlayerState·권위 ASC/Instant GE·공유 ViewModel과
모의 backend를 사용한다. 마지막 한 개/잔여 스택의 응답 유실→동일 본문→1회 회복,
401/변조 거절·큰 revision/terminal 보존·완료 캐시, GAS 필터 거절 후 원래 magnitude
복구/재진입 차단, Pawn 교체·새 실행 범위·정의 미준비/신원 회수를 확인했다.

에디터가 열려 있을 때 자체 `-NoLink` 6개 및 정리 후 6개 최소 컴파일을 성공했다.
에디터가 외부에서 종료된 것을 확인한 뒤 자체 정상 Editor 타깃 4개 작업·게임 DLL 링크/
타깃 exit 0(8.98초), 기존 `Run-ItemAutomationTest.ps1 -InventoryRecoveryOnly -SkipSDKSetup`
소스 유래 **47/47**·러너 exit 0이다. 모듈 `B74E4A0D…0B607`·선택 입력 61개(중복 0)/닫힌
로그의 실행 전후가 일치했다. 기동 전 기존 Error 15줄은 남으며 검사 중/이후 Error 0·전체
Fatal/Assert/Ensure 0이다. 이전 가방/우편/장착 실통합은 반복하지 않았고 타 작업의 에디터·
프로세스나 BP/에셋을 종료/수정하지 않았다.

증거: `Saved/Tests/ConsumeRecovery-cb1278a4/Verification.proof.json`;
native: `Saved/Tests/Item/Automation-b4459eecfa3241299569849f2974638b/Item.proof.json`.
이는 실제 HTTP/PostgreSQL/Iris/Slate 입력·서버 재시작·Steam/Shipping 결과가 아니다.
당시 후속이던 가방/퀵슬롯의 실제 소비 입력과 HTTP 응답 유실 연결은 아래 실연결에서
완료했다. 서버 재시작 효과 복구는 휘발성 전투 상태의 연속 플레이 요구가 확정될 때의 별도 범위다.

### 전투 불능·새 전투 세대의 소비 차단 (2026-10-29, 구현·분리 native 완료)

이 절은 10-29 당시의 구현/검증 기록이다. 확정 차감 뒤 사망/세대 변경을 계속 보류하던
정책은 아래 10-30 후속 구현으로 변경했으며, 당시 8/8은 새 완료 정책의 실행 증거가 아니다.

현재 일반 소모품은 부활 명령이 아니다. 기존 가방/퀵슬롯의 Controller → `ServerConsumeItem`
→ 준비/커밋 경로에서 원래 권위 Pawn/ASC에 유한한 양수 체력과 사망 태그 부재를 요구한다.
이미 전투 불능이면 HTTP 전 `PreparationRejected`로 거절해 수량/Revision을 보존하며 가짜
미확정 복구를 만들지 않는다. 별도 BP/RPC/아이템 정의·백엔드 스키마는 바꾸지 않았다.

요청을 보낸 뒤 사망했다면 성공/AlreadyApplied 영수증이 있어도 회복 GE를 실행하지 않는다.
기존 정책대로 확정된 아이템 투영은 반영될 수 있지만 원래 미전달 효과와 요청/전체 본문·
공유 BP 복구 상태는 남는다. 사망을 이유로 차감을 되돌리거나 새 요청을 발급하지 않는다.
원래 Character의 기존 `GetCombatPresentationRevision()`도 준비 때 고정해 같은 Pawn/ASC가
전투 복구로 새 세대가 된 경우 이전 효과가 넘어가지 않게 했다. 소비 효과 포기/환불·전투
세대를 넘는 복구 해소 정책은 이 수정으로 정하지 않으며, 준비된 효과의 임의 해제도 없다.
스턴/에어본 등 다른 제어 상태의 새 제한이나 부활 아이템 정책을 추가한 것은 아니다.

변경 CPP 두 개를 독립 객체로 컴파일하고 별도 RuntimeProject DLL에 링크했다. 기존 링크
응답을 재사용하되 프로젝트 객체 434개/PCH·응답을 사본으로 고정했다. 기존 헤더/라이브러리는
읽기 전용으로 사용했고 원본 Intermediate/공유 DLL을 덮어쓰거나 에디터·다른 담당 검사를
변경하지 않았다. 전체 UBT 타깃/정상 모듈 반영은 아니다.
기존 러너의 `-ConsumeRecoveryOnly`는 소스에서 이름을 추출해 소비 사례만 실행한다.
새 3종과 직접 영향 기존 5종 **8/8·러너 종료 0**, 선택 입력 61개(중복 0)/실제 로드한
분리 DLL `19A45B17…483762C`·닫힌 로그 일치를 확인했다. 기존 기동 Error 15줄은 별도이며
검사 중/이후 Error 0·전체 Fatal/Assert/Ensure 0이다.

첫 7/8 실패는 새 native Player의 시작 데이터 누락으로 기존 possession ensure가 발생한
테스트 준비 문제였다. 테스트에만 transient 빈 시작 데이터를 지정해 수정했고 원래
ensure/운영 코드는 유지했다. 실패 로그/proof와 당시 DLL을 보존하고 새 실행만 성공으로
기록했다. 같은 Pawn의 세대 검사는 native에서 기존 서버 필드만 올린 모의 상황이며 실제
패배/재시도 흐름의 증거는 아니다. 실제 GAS/모의 backend로 확인했고 완료된 HTTP/PG·
가방/퀵슬롯 실통합이나 전체 아이템 검사는 반복하지 않았다.

근거: `Saved/BuildChecks/ConsumeLivingAvatar-20261029/Verification.proof.json`.
다음 조율된 정상 Editor 빌드/재실행에 반영해야 하며 현재 공유 DLL/실제 BP 플레이에
반영됐다고 표시하지 않는다. 서버 재시작 효과 복원·Steam/Shipping은 기존 조건부 범위다.

2026-10-30 새 자동 폐기 지침에 따라 이 검증의 남은 Inputs/Objects·초기 DLL·
RuntimeProject/캐시를 폐기했다. 이전의 DLL/입력 보존 설명은 당시 상태이며 같은 바이너리
재실행·고정 입력 재감사는 더 이상 가능하지 않다. 원래 wrapper proof·명령/입력 해시는
그대로 남기고, 첫 실패/최종 성공의 native proof와 필수 automation 로그 4개는
`Saved/Maintenance/ItemVerificationCleanup-20261030/Evidence/`로 동일 해시 복사했다.
backend 검증 사본 3개를 포함한 정리 결과는 같은 디렉터리의 `Result.json`에 기록했다.
게임 소스/유지 테스트·스크립트·원본 DLL/에셋은 보존했으며 새 게임 검사는 실행하지 않았다.

### 확정 소비의 사망·전투 세대 변경 후 종료 (2026-10-30, 구현·CPP 개별 컴파일)

사용자의 다음 작업 지시에 따라 제안한 **차감 확정·효과 소멸** 정책을 연결했다.
기존 소유자/실행 범위·전체 원래 영수증 검증과 Projection 적용을 통과하고 원래
현재 Pawn/권위 ASC가 유효할 때만, 사망 태그 또는 유한한 체력 0 이하·기존 recovery
revision 변경을 확인한다. 이 경우 GE/환불/새 요청 없이 원래 효과를 종료 캐시에 봉인하고
준비 Spec·대기 효과를 회수한다. 이후 같은 요청은 기존 완료 캐시로 끝나므로 체력 복구나
새 전투에서도 이전 효과가 재생되지 않는다. 사전 전투 불능의 HTTP 전 거절도 유지한다.

응답 유실/401/404/충돌·변조 영수증은 완료 증거가 아니며 그대로 보류한다.
Pawn/ASC 교체·파괴/부재, 같은 세대의 누락/비유한 체력, 일반 GAS 적용 거절도 임의로
완료하지 않는다. 서버 재시작 효과 복구나 보상/환불 정책으로 확장하지 않았다.
기존 오케스트레이터 Completed → HandleOutcome → owner-only InventoryRecovery 제거와
공통 ViewModel 갱신을 사용하므로 BP/RPC·새 enum/위젯/에셋·백엔드 변경은 없다.
기존 안내는 서버의 소비 확정을 뜻하며 실제 회복 성공/효과 소멸 전용 안내를 추가한 것은 아니다.

기존 소비 테스트의 사망·세대 변경 기대를 새 정책으로 바꾸고, 마지막 1개/잔여 스택·
체력 0/독립 사망 태그·최초 성공/유실 뒤 재확인·완료 후 재시도, 미확정/변조 거절 뒤
원래 영수증으로만 해소하는 사례를 반영했다. 기존 교체/적용 실패/범위·회수 사례도 유지한다.
이 개별 컴파일 시점의 선택 이름은 소스 유래 9개이며 **당시에는 실행하지 않았다**. 두 CPP의 별도 CL 컴파일만
종료 0·경고/오류 0, 선택 입력 62개·사본 5개/원본 보호 4개 해시 불변을 확인했다.
공유 UHT/UBT/Intermediate·게임 DLL과 열린 에디터/다른 담당은 유지했다.
자체 PCH/응답/OBJ Temp 2,591,444,677 bytes(2.4135 GiB)는 종료 후 자동 폐기했으며
잔여/정리 예외가 없다. 원본 보호 해시는 당시 비교 증거이고 폐기한 입력/객체는 재사용할 수 없다.

근거: `Saved/BuildChecks/ConsumeTerminalResolution-20261030/Compilation.proof.json`.
당시 다음 단계였던 선택 9개 실행은 아래 분리 native에서 완료했다.
개별 컴파일 자체는 정상 타깃/링크/native·실제 BP 잠금 해제/HTTP·PG/Iris/실제 패배 복구
성공이 아니며 이전 8/8로 새 정책을 대체하지 않는다.

### 확정 소비 종료의 분리 native 실행 (2026-10-30, 9/9)

제품 코드를 다시 바꾸지 않고 기존
`Scripts/Tests/Run-ItemAutomationTest.ps1 -ConsumeRecoveryOnly -SkipSDKSetup`을 별도
RuntimeProject에서 실행했다. 원본 DLL을 사용하는 사용자 에디터 27576/LiveCoding과
공유 UHT/UBT/Intermediate는 유지했다. Source/생성 헤더·프로젝트 OBJ/LIB/RES와 사용한
SingleFile 객체를 사본으로 고정하고 소비 2CPP·생성 묶음 3개/기존 Lobby CPP를 별도 CL로
컴파일했다. 미빌드인 무관 테스트 6개만 링크에서 제외해 전체 타깃 성공으로 계산하지 않는다.

최종 CPP 6개/분리 링크 종료 0·경고/오류 0, 소스 유래 **9/9·UE/러너 종료 0**이다.
실제 로드한 전용 DLL의 경로/해시를 자체 UE PID 6068에서 관측했다.
원래 첫 성공/응답 유실 뒤 성공 영수증, 마지막 1개/잔여 스택, 체력 0/독립 사망 태그와
같은 Pawn의 새 전투 세대에서 회복·환불 없이 완료/준비 효과 회수·공통 ViewModel 복구 잠금
해제를 확인했다. 완료 캐시의 재시도는 추가 HTTP 모형 호출/효과 실행 없이 끝났다.
응답 유실/401/404/리비전 충돌·변조 영수증, Pawn 교체/새 실행 범위·GAS 적용 거절/신원
회수의 기존 보존 경계도 통과했다. 순수 native의 GAS/모의 backend·기존 복구 상태 연결이며
실제 BP 클릭/owner-only 네트워크 전송·실제 패배/재도전이나 HTTP/PG 정산의 증거는 아니다.

선택 원본 65개·실행 입력 61개·원본 보호 3개/실행 DLL·필수 로그 해시가 일치했고
기동 Error 15줄과 검사 중/이후 Error 0·전체 Fatal/Assert/Ensure 0을 구분했다.
첫 사본의 스킬 헤더/UHT 시차 컴파일 실패와 검증 도구의 상대/SDK 라이브러리 경로 수집
실패 두 번은 `Initial/Second/Third`에 보존했다. 원본이나 제품 소스를 고친 것이 아니라
갱신된 입력을 다시 고정하고 검사 도구의 입력 범위를 바로잡은 뒤 최종 실행에 성공했다.
네 시도의 Temp/객체/분리 DLL·PDB/Source 사본/UserDir·캐시를 모두 자동 폐기했다.
합계 8,476,617,353 bytes(7.8945 GiB), 잔여/정리 예외 0이며 작은 결과·명령/해시/필수
로그만 남긴다. 폐기한 DLL/입력은 재실행·고정 입력 재감사에 사용할 수 없다.

결과: `Saved/BuildChecks/ConsumeTerminalNative-20261030/Verification.proof.json`;
native 결과/필수 로그는 같은 디렉터리의 `Item.proof.json`/`automation.log`다.
이 실행 당시에는 정상 공유 모듈 교체·실제 게임/BP 반영이 남아 있었다.
후속 정상 DLL 포함 확인은 아래 기록을 따르며, DLL 변경만을 이유로 완료한 9개나
이전 HTTP/PG·전체 아이템 suite를 반복하지 않는다.

### 소비 종료 구현의 정상 DLL 포함 확인 (2026-10-30, 기존 빌드 근거 대조)

다음 작업 재개 시 다른 담당의 정상 설치가 이미 끝난 것을 확인해 중복 빌드하지 않았다.
현재 소비 컴포넌트 소스 `7D582A59…E405AB8`은 TrainingActivation의 성공한 102개 NoLink
입력과 같고 해당 CPP의 실제 컴파일/성공 로그가 남아 있다. 그 객체
`2B4E1D73…68B2829`가 Legacy의 설치 링크 입력과 현재 원본 객체에서도 동일하다.
ActionProgressInstall의 정상 타깃 성공(재컴파일 0/링크 34개)·원본 DLL 로드/종료 0 기록과
조회 시 설치 DLL `4BCAB2FB…7B764E7`도 일치한다. 따라서 이 소비 종료 구현의
**원본 DLL 반영 대기는 해소됐다**. 실제 호출 경로/완료 캐시·복구 상태 연결도 변경되지 않았다.

이는 다른 담당이 수행한 빌드의 소스→객체→설치 근거 대조이지 이번 새 빌드/게임 실행이 아니다.
소비 테스트 객체는 기록 사이 달라졌으므로 기존 분리 DLL의 9/9를 설치 DLL의 9/9로
표시하지 않는다. 같은 소비 소스의 기존 9개나 HTTP/PG·광역 검사는 반복하지 않았다.
실제 사망 중 소비의 HTTP/Iris/BP 조작 연결과 다른 담당의 최신 소스 트리 전체 반영은 별도다.
후속 에어본 담당이 원본 DLL/공유 빌드를 예약했으며 이 확인을 위해 되돌리거나 교체하지 않는다.

근거: `Saved/BuildChecks/ConsumeInstalledHandoff-20261030/Provenance.proof.json`.
문서/작은 근거 요약만 갱신했고 코드/에셋/설정·DLL·에디터/다른 담당은 수정/실행하지 않았다.
임시 사본/캐시 생성·삭제 0, 정리 예외 없음이다. 설치 DLL 해시는 조회 당시 기준이며
이후 다른 담당이 만든 모듈까지 같은 결과라고 확장하지 않는다.

### 사망 소비 실연결용 원래 복구 응답 보류 (2026-10-31, 프록시 준비 완료)

기존 `InventoryRecoveryHttpProxy.mjs`에 Consume 전용 명시 opt-in
`holdRecoveryResponse: true`를 추가했다. 새 서버/위젯/보상 API를 만들지 않고 기존의
첫 커밋 응답 유실 → 원래 복구 버튼 → 같은 본문의 AlreadyCommitted 재확인 흐름을 재사용한다.
`/arm`의 실행별 소유자·아이템·서버 자격 해시 검사는 유지하고 다른 작업의 활성화나
잘못된 플래그 타입은 400으로 거절한다. 옵션을 생략하면 기존 동작 그대로다.

기존 `/release` 뒤 재시도의 **전체 검증된 원래 응답**만 한 번 보류해 상태를
`recovery-response-held`로 표시한다. 같은 loopback 제어 인증의
`POST /deliver-recovery`는 원래 status/body 바이트를 재직렬화 없이 해제한다.
새 영수증/요청/ID를 만들지 않으며, 해제 횟수는 클라이언트 완료 증명이 아닌 전송 제어 기록이다.
복구 버튼은 살아 있을 때 누르고 이 보류 구간에 승인된 피해로 사망시킨 뒤 응답을
해제하는 UE 접합이 다음 단계다. **이번에는 사망/게임 흐름을 실행하지 않았다.**

보류 중 동일 요청의 중복 호출은 upstream 추가 호출 없이 409, 인증 없는 해제는 403,
보류 전/중복 해제는 409다. 다른 frozen 바이트/변조 backend 영수증은 완료로 인정하지 않는다.
보류 중 연결 종료는 실패로 봉인해 나중 해제를 막고, 프록시 종료 시 보류 소켓/버퍼도
회수해 검사 프로세스가 매달리지 않게 했다. 바디/자격/영수증 원문은 로그에 남기지 않는다.

새 6종(원문/1회 해제·권한/중복, opt-in 범위, 본문 변조, 종료, 연결 해제, 변조 복구
영수증)과 직접 영향을 받는 기존 프록시 38개, **44/44·Node 종료 0**이다.
입력 두 파일의 실행 전후 해시·필수 로그 해시가 일치하며 실행별 소켓은 `t.after`와 종료
처리로 닫혔다. 원본 C++/BP/설정·모듈/backend/DB는 수정/실행하지 않았고, 완료한 native9/
UE·HTTP/PG 전체 검사를 반복하지 않았다. 임시 실행 사본/대형 산출물 생성·삭제 0/정리 예외 없음이다.

근거: `Saved/BuildChecks/ConsumeRecoveryResponseHold-20261030/Verification.proof.json`.
이는 loopback Node와 모의 upstream의 프록시 준비 결과다. 기존 UE 서버 관측기의 승인
사망 피해/원래 Pawn·ASC·0회 효과 관측과 클라이언트의 복구 WBP/ViewModel 해소 판정을
새 opt-in 경로에 연결하고 실제 backend/Iris 실행하는 작업은 아직 남아 있다.

### 사망 소비 실연결의 실행·판정 스크립트 준비 (2026-10-31, UE 접합 대기)

`Scripts/Network/Run-DungeonBossReturnE2ETest.ps1`에 명시적인
`-InventoryRecovery -InventoryRecoveryOperation Consume -ClientCount 1 -InventoryRecoveryConsumeDeath`
모드를 추가했다. 현재 세션의 가방 입력만 허용하고 퀵슬롯·재시작·자격 실패·다른 전투
시나리오를 섞지 않는다. 새 client/server 관측 문자열이 정상 DLL에 없으면 실행별
디렉터리·프로세스·DB 기록을 만들기 전에 거절한다. **아직 아래 UE 관측기 수정이 없어
실행 가능한 완성 모드가 아니다.** 기존 일반 소비 경로는 별도로 유지한다.

`Fixtures/InventoryRecovery.ps1`은 기존 첫 응답 유실/같은 복구 버튼/원래 영수증 비교를
재사용한다. 검증된 재응답 보류 → 원래 request/source/owner의 사망 게이트 → 서버 승인
피해와 소유자 `DEAD_WAIT` 관측 → 원래 DB 영수증/개수 불변 확인 → 같은 원문 응답 전달
순서다. 회복 효과 0회·체력 0·동일 Pawn·원래 요청의 6개/revision2 반영과 기존 BP 복구
상태 해소를 모두 확인해야 성공하도록 판정을 준비했다. 새로운 요청/환불·직접 HP 쓰기나
UI/RPC 우회 제출을 추가하지 않았다. 이 순서는 스크립트 계약이며 실제 게임 성공 증명이 아니다.

새 모드는 자기 UE/proxy/backend 종료를 확인한 뒤 `finally`에서 해당 실행의
`Saved/Tests/DungeonBossReturn/<runId>/backend-build` 및 `Saved/Tests/DBR/<12자리>/`만
자동 폐기한다. 정확한 경로·reparse point·파일 독점 열기를 검사하고, 종료 확인 실패나
정리 실패를 성공으로 남기지 않는다. `consume-cleanup.json`과 작은 결과/로그·해시는
유지하며 원본 DLL/Intermediate/에셋과 DB 데이터는 삭제하지 않는다.

집중 스크립트 확인 4그룹 통과: 두 파일 구문, 정상 모드 허용/부적절한 조합 14종 거절,
모의 로그의 정상 순서 및 누락/조기 완료/효과 발생 거절, 자기 임시 경로 정리와 실행 중
writer/불명확한 종료/넓은 대상 거절이다. 마지막 입력2개의 SHA256은 실행 전후 동일하다.
최종 시험 임시 1,462 bytes, 세 차례 소규모 확인 합계 4,107 bytes 자동 정리·잔여/예외0이다.
근거: `Saved/BuildChecks/ConsumeDeathIntegration-20261031/Scripts.proof.json` 및
같은 디렉터리의 `Verify-Scripts.ps1`. UE/backend/PG/Node 프록시는 실행하지 않았고 기존
native9/프록시44/일반 소비 E2E를 반복하지 않았다.

최초 Scripts 준비 중에는 `training-lobby-profile-20261031`의 정상 빌드 예약을 우선
인계했다. 이후 해당 담당의 완료/전체 해제를 확인해 공유 자원을 재등록하고 기존
`RPGConsumeRecoveryE2E.{h,cpp}` 두 파일에 원래 request/source/owner·재시도 상태를
대조하는 사망 게이트, 기존 승인 피해의 치명 피해 적용, 동일 Pawn/효과0·완료 projection과
종료 관측을 소스로 추가했다. 직접 HP/사망 태그 변경이나 제품 정책 변경은 없다.

그러나 같은 시점 `platform-player-security-20261031`도 직전 Scripts 종료 기록을
근거로 공유 자원을 재예약한 것을 두 파일 편집 직후 확인했다. 추가 Source 편집·빌드·
UE 실행 전에 중단하고 해당 담당에게 우선권을 인계했으며 본인 예약은 해제했다.
서버 두 파일은 **컴파일/실행 미검증**, `RPGInventoryRecoveryE2E.{h,cpp}`의 클라이언트
접합은 **미구현**이다. 따라서 새 모드는 DLL 사전검사에서 계속 거절하며 실제 게임
성공으로 표시하지 않는다. 현재 소스 해시는 `ServerSource.proof.json`에 별도로 기록했고
이는 Scripts 통과나 이후 다른 담당의 빌드 성공을 사망 소비 E2E 성공으로 확장하는 근거가 아니다.
본인 대형 산출물/추가 프로세스는 생성하지 않았다.

다음은 platform 담당 종료/인계 확인 후 클라이언트의 원래 Pawn/ASC·사망 대기/완료
판정을 접합하고, 정상 빌드와 이 새 시나리오만 실행하는 것이다. 공유 입력의 검증 중에는
위 서버 소스도 다시 변경하지 않는다.

2026-10-31 후속 재개에서도 같은 담당의 전체 Source/빌드 입력 예약이 유지돼 원본
소스는 편집하지 않았다. 남은 클라이언트 접합은 별도
`Saved/BuildChecks/ConsumeDeathIntegration-20261031/ClientPending/ClientDeathRecovery.apply_patch`
로 준비했다. 가방 Consume 전용 옵션, 원래 Pawn/ASC 고정·교체/체력 무효 거절,
응답 보류 중 체력0/원래 projection·복구 잠금 관측, 원래 AlreadyApplied와 6개/revision2·
체력0·잠금 해소의 완료 판정이다. 일반 살아 있는 소비와 작성된 WBP/공통 Model은
유지하며 새 RPC·체력 쓰기·UI 우회나 제품 정책 변경을 추가하지 않는다.

두 원본의 SHA256을 고정하고 **9개 패치 문맥이 각각 유일하게 일치함을 메모리에서만
확인**했다. 이는 C++ 구문/빌드/native/E2E 검사가 아니며 원본에는 아직 적용하지 않았다.
근거는 같은 디렉터리의 `Readiness.proof.json`이다. 원본/공유 출력·에셋/backend/DB와
다른 담당의 프로세스는 변경하지 않았고 임시 실행 사본/캐시 생성·삭제0/정리 예외 없음이다.
작은 패치와 준비 기록은 후속 적용용 최종 인계물로 보존한다. 실제 인계 뒤에는 **현재**
원본 해시/패치 문맥을 다시 대조하고 Source 두 파일·공유 빌드/실행을 재예약한 다음
적용해야 한다. 기존 Scripts4그룹/서버 소스 기록을 실제 사망 소비 성공으로 확대하지 않는다.

### 사망 소비 관측기와 클라이언트 후보의 독립 컴파일 (2026-10-31)

선행 platform-player-security는 종료됐지만 후속 `airborne-platform-network-20261031`이
전체 Source/공유 빌드·검증 입력을 사용 중이고 사용자 에디터2584도 유지 중이다.
원본에 패치를 적용하거나 정상 DLL을 바꾸지 않고, 자기 Saved의 후보 h/cpp에만 패치를
적용해 기존 MSVC 응답에서 PCH/출력/진단 경로를 분리했다. UBT/UHT·링크·UE/backend/
DB와 다른 담당의 프로세스를 실행·수정하지 않았다. 헤더/응답을 읽을 뿐 다른 담당의
수정을 막는 입력 예약을 요구하지 않으며 사용된 입력이 변경되면 성공으로 인정하지 않는다.

최초 클라이언트 컴파일에서 `UE_LOG` 매크로와 중괄호 없는 if/else가 충돌하는 C2181을
발견했다. 후보와 후속 적용용 패치의 양쪽 분기에 중괄호를 추가했고 제품 로직·원본 Source는
수정하지 않았다. 초기 파일 읽기/PS5 진입기의 준비 문제도 컴파일 실패와 구분했다.
최종 후보의 아래 **4개 CPP 개별 객체 컴파일은 모두 종료0/오류0/경고0**이다.

- `RPGInventoryRecoveryE2E.cpp`: 사망 중 원래 Pawn/ASC·옛 projection/잠금과 회복 없는 완료 판정의 클라이언트 후보.
- `RPGConsumeRecoveryE2E.cpp`: 이미 추가된 서버 승인 사망 피해·원래 요청/효과0 관측기의 현재 원본 소스.
- `RPGInventoryStackSplitE2E.cpp`: 후보 헤더를 사용하는 클라이언트 probe의 직접 생성/호출부.
- `RPGDungeonFlowE2EProbe.cpp`: 서버 probe의 직접 생성/호출부.

준비한 입력/제어1,923개 중 실제 사용한 프로젝트 입력·제어 **192개**를 컴파일 전후와
최종 감사에서 대조해 불변을 확인했다. 최종 후보와 보완된 패치의 9문맥 적용 결과도
메모리에서 일치한다. 후보 CPP `DBFA82C9…FF53395`, 헤더 `7841561D…DB40C8`,
패치 `0C0FA544…B96C2C`다. 이는 원본 클라이언트 패치 적용·전체 타깃 빌드/링크·native/
실제 사망 소비 HTTP/PG/Iris/WBP 성공이 아니다. 기존 native9/프록시44/일반 소비 E2E도
반복하지 않았다. 앞의 서버 소스 미검증 기록은 당시 상태이며 이번은 그 소스의 객체 컴파일만 해소한다.

근거: `Saved/BuildChecks/ConsumeDeathIntegration-20261031/CompileSummary.proof.json`.
초기 C2181은 `Compile/3cd15f022e0846faad4c91685ccc05f6/`, 최종 4개 성공은
`Compile/6efcb3f4e872417ba1d30d4b8558871d/`에 작은 명령/입력·객체 해시/필수 로그를
별도로 보존했다. 자기 compiler 종료·예약/절대 경계/reparse·파일 사용 해제를 확인한
`finally`에서 두 시도 임시 객체/진단/의존성/rsp **209,712,647 bytes(199.9976MiB)**를
자동 폐기했다. 잔여/예외0, 정상 출력과 사용자 에디터/에셋/backend/DB는 유지했다.
폐기한 객체 해시는 당시 증거이며 같은 객체의 후속 링크/재실행을 약속하지 않는다.

본인 독립 준비 예약은 해제한다. 다음은 airborne 담당 종료/인계 후 원본 두 파일의
현재 해시/문맥을 다시 대조하고 원본 적용·공유 빌드/실행 자원을 순차 재예약하는 것이다.
정상 DLL 교체는 사용자 에디터의 사용 상태도 확인해야 하며 임의로 종료하지 않는다.

### 사망 소비 클라이언트의 원본 적용과 정상 NoLink (2026-10-31)

airborne-platform-network의 종료/전체 예약 해제를 확인하고 원본 두 파일 및 공유
UHT/UBT/Intermediate의 NoLink 범위를 순차 예약했다. 다른 담당의 로비/PIE 독립 실행과
사용자 에디터는 유지했다. 기존 호출부와 적용 전 해시를 재확인한 뒤 준비한 보완 패치를
`Private/Tests/RPGInventoryRecoveryE2E.{h,cpp}`에 적용했다. 두 원본은 앞서 확인한
후보와 **바이트 단위로 일치**한다(헤더7841561D…DB40C8/CPP DBFA82C9…FF53395).
같은 패치를 별도로 다시 적용하면 안 된다. 기존 서버 관측기/Scripts·다른 Source/에셋/
권한·아이템 정책/backend 제품 코드는 이번에 편집하지 않았다.

정상 `Project_RPGEditor Win64 Development -NoLink`가 **종료0/Succeeded**다.
`RPGInventoryRecoveryE2E.cpp`와 직접 생성/호출부 `RPGInventoryStackSplitE2E.cpp`
두 CPP 재컴파일, 링크0·컴파일 오류/경고0이다. 기존 StructUtils 플러그인의 사용 중단
안내는 별도로 남아 있다. 입력1,798개와 보호 설치 바이너리83개는 빌드 전후/최종 감사에서
불변이다. 현재 서버 관측기/두 호출부의 정상 객체 해시도 요약에 기록했지만, 이 객체를
사용한 정상 DLL 링크나 실제 게임 실행의 성공으로 계산하지 않는다.

근거: `Saved/BuildChecks/ConsumeDeathIntegration-20261031/Applied/Summary.proof.json`,
실행별 `Applied/cb2a145870a245fab42ea1384cf53ddb/{Command,Inputs,ProtectedBinaries,Result}.json`
및 작은 Console/Build 로그다. 정상 Intermediate는 유지할 빌드 출력이다. 별도 임시
PCH/OBJ/DLL/UserDir 사본은 생성하지 않았으므로 이번 단계 임시 생성/삭제0 bytes·
정리 예외 없음이며 정상 출력/사용자 작업은 삭제하지 않았다.

설치 DLL은 여전히 `9968DDA8…EA32034`이고 새 클라이언트 `CONSUME_DEATH_PASS`를
포함하지 않는다. 사용자 GUI2584가 원본 모듈을 로드한 것을 읽기 전용으로 확인했다.
따라서 **원본 소스 적용과 정상 타깃 컴파일만 완료**, DLL 반영과 실제 사망 소비의
HTTP/PG/Iris/WBP 검증은 미완료다. 기존 검사를 반복하거나 실행 중 에디터를 종료하지 않았다.

본인 Source/공유 NoLink 예약을 해제하며 progression-equipment-transport 담당의
원본 적용은 그 담당이 최신 경로/문맥을 다시 확인해 순차 예약할 수 있다. 다음 정상 DLL
교체는 에디터의 모듈 사용 해제와 최신 공유 예약/입력을 확인한 뒤 별도로 진행한다.
인계 후 다른 Source/정상 객체가 바뀌면 이번 해시와 NoLink 성공은 당시 증거로만 취급한다.

### 사망 복구 실행기의 backend 소스/출력 분리 (2026-11-01)

정상 설치를 중복 빌드하지 않고 시작 시점 F1A20269…E1AC5654의 client death pass·
server death resolved·옵션 문자열을 읽기 확인했다. 다른 담당의 UE Source/공유 빌드와
backend API/저장 작업을 보존하며, 기존 러너가 backend 원본 csproj를 직접 빌드해 원본
obj에도 쓸 수 있는 부분을 독립 작업으로 선택해 수정했다. 실제 게임 실행은 하지 않았다.

기존 `-InventoryRecoveryConsumeDeath` 모드만 새
`Scripts/Network/Fixtures/InventoryConsumeBackendSnapshot.ps1`을 사용한다.
자기 `backend-build/inputs` 아래에 애플리케이션·Database SQL/마이그레이션·프로필과
상위 빌드 제어 파일을 복사하고 원본 bin/obj/artifacts는 제외한다. 복사 전후 파일 집합/
전체 cohort 바이트를 확인한 뒤 **복사된 csproj**를 빌드하므로 원본 backend obj를 사용하지 않는다.
실행 DLL 출력은 같은 backend-build 아래의 `runtime` 형제 경로로 분리하고 backend 시작/
작업 디렉터리·영수증 결과의 assembly 해시도 그 경로를 사용한다. 실제 SDK9 targets가
OutputPath 하위를 기본 Compile에서 제외하므로 소스 사본을 포함하는 부모에 출력을 두지 않는다.
고정되지 않은 ProjectReference/명시 Import와 경로 이탈·reparse point·대상 재사용은
거절한다. 이는 애플리케이션 입력/출력 분리이며 악성 MSBuild/SDK를 위한 sandbox는 아니다.

빌드 직후와 자기 프로세스 종료 뒤 사본 입력을 확인하며, 변경되면 성공 결과를 false로
남기고 실패시킨다. 검사 실패 때도 기존 `finally` 정리가 수행돼야 하며 inputs와 그 안의
obj는 이미 할당된 backend-build 정리 범위와 함께 폐기한다. 작은 소스 cohort 해시와
종료 확인만 runRoot에 남기고 파일 본문/자격을 로그에 남기지 않는다.
새 사본에는 복원 결과가 없으므로 이 모드의 `-NoBackendRestore` 혼합은 디렉터리/DB/
프로세스 생성 전 거절한다. 다른 모드의 기존 빌드 경로/옵션은 바꾸지 않았다.

새 유지 검사 `Scripts/Tests/Test-InventoryConsumeBackendSnapshot.ps1`에서 작은
독립 fixture의 최종 9그룹 통과: 입력/SQL/프로필/제어 파일 보존·출력 제외, 원본의 이후 수정과
닫힌 사본 분리, 사본 변조/추가 파일, 대상 재사용/넓은 경로, 두 종류 Junction과 대상 보존,
외부 프로젝트/직접·제어 파일 Import, 복사 중 원본 변경, fresh restore 옵션 거절이다.
추가 MSBuild item/property 평가에서 형제 runtime 출력일 때 복사 Program.cs가 Compile에
포함되고 obj 경로도 자기 inputs 안임을 확인했다. 이 평가는 build target을 실행하지 않는다.
PS4 구문과 최종 입력4 해시도 동일하다. 제어 파일 Import/종료 시 사본 확인과 SDK 출력
경로를 보완한 단계별 확인이며, 기존 광역 검사·후보 컴파일을 반복한 것이 아니다.
세 작은 Temp 총 **3,039 bytes** 자동 정리·잔여/예외0이고 실제 backend 복사/빌드·
UBT/UE/서비스·DB 쓰기는 없다. 근거:
`Saved/BuildChecks/ConsumeBackendSnapshot-20261101/Implementation.proof.json`.

당시 추가 읽기 조회에서 Docker/개발 PG healthy, 기존 public.item_records는 존재하지만
equipment_progression 컬럼은 없음을 확인했다. 현재 Development backend는 시작할 때
v2 델타를 포함한 초기화 SQL을 적용하므로 **기존 개발 DB 스키마 갱신 허용 여부 확인 전에는
실제 E2E backend를 시작하지 않았다**. 격리 DB를 선택한다면 기존 러너의 고정 compose/
DB assertion 경로도 명시적으로 분리해야 하며, 현재 완료로 표시하지 않는다.
작업 도중 설치 DLL이 다른 담당에 의해 2B2B45B1…28E3898로 바뀐 것도 별도 관측했다.
F1의 문자열 확인을 새 DLL의 실행 성공으로 확대하지 않으며, 실제 실행 전 현재 모듈/
입력/공유 예약을 다시 확인한다. 본인 PS/fixture 예약은 해제한다.
이후 사용자 승인에 따른 개발 DB 적용은 바로 다음 절에 별도로 기록한다.

### 기존 개발 DB의 v2 적용과 사망 복구 선행 조건 (2026-11-01)

사용자가 기존 DB 적용을 승인해 로컬 `127.0.0.1:54329/project_rpg`에 기존
`0002_item_equipment_progression.sql`만 커밋했다. 운영 이력이 없는 개발 DB이므로
전체 v1 재생/운영용 migration history 생성은 하지 않았다. 공유 UE 빌드/정상 DLL·
에셋/다른 담당 프로세스는 변경하지 않았고 backend 서비스도 시작하지 않았다.

제한 ACL의 최종 전체 백업을 먼저 확보한 뒤 기존 스키마 advisory 잠금/제한된 대기·
단일 transaction을 사용했다. public 25테이블/18,371행의 전후 행 수·SHA256 지문이
같고, 기존 아이템 10,759행은 새 nullable JSONB 값이 모두 NULL이다. CHECK의
validated 상태와 컬럼/no-default·운영 이력 부재도 새 연결로 재확인했다.
로그 JSON 줄바꿈에 따른 보고 오류는 기존 로그/읽기 전용 확인으로 복구했으며
DDL 재실행은 없다. 최초 실패 결과와 최종 성공 근거를 구분해 유지한다.

근거: `Saved/BuildChecks/DevDbV2-20261101/e13d2e7b8a484529b37b6238475fd414/FinalResult.json`.
최종 복구용 `Saved/Backups/Database/DevV2-e13d2e7b8a484529b37b6238475fd414/Before-v2.dump`
4,679,864 bytes는 보존한다(목록/복사 SHA 확인, 실제 복원 리허설 아님).
컨테이너의 자기 임시 덤프 4,679,864 bytes는 삭제·잔여/정리 예외0이다.
자세한 [DB 적용 경계](../Backend/POSTGRES_SCHEMA_INITIALIZATION.md#기존-로컬-개발-db의-v2-적용--2026-11-01)를 따른다.

기존 개발 DB 갱신 승인/컬럼 누락 선행 조건은 해소됐지만, 이것이 실제 사망 소비 복구
E2E 통과를 뜻하지 않는다. 다음에는 최신 Source/설치 DLL/공유 예약을 확인·재예약한 뒤
기존 `-InventoryRecoveryConsumeDeath` 경로에서 HTTP/PG/Iris/WBP의 동일 요청·사망 후
회복 없음·한 번만 차감·기존 잠금 해제를 확인한다. 이번 요청의 적용 범위에서 게임 검사는
실행하지 않았다.

### 사망 후 소비 복구의 실제 HTTP/PG/Iris/WBP 확인 (2026-11-01)

개발 DB v2 적용 후 기존 `-InventoryRecoveryConsumeDeath` 경로를 실제 한 클라이언트와
권위 서버에서 완료했다. menu-frontend의 Source/공유 빌드와 HUD 에셋 저장을 유지하기 위해
자기 `Saved/BuildChecks/ConsumeDeathRuntime-20261101/Run.ps1`에서 기존 실행기의
UE/Config/Plugins/Network/Backend 입력을 단기 ReadWrite/Delete 공유로 복사하고
파일 집합·cohort 해시를 확인했다. 다른 담당이 수정 중인 PlayerHUD와 인벤토리 Projection은
물리 사본 overlay, 나머지 Content/플러그인 자산은 읽기 링크로 사용했다. 원본의 장기
입력 고정/모듈 매핑·Source/에셋 저장·UHT/UBT/정상 DLL 쓰기는 하지 않았다.

실제 로드 DLL은 `6CE001595DE7C5DE474EA98CC7F3726AB3F4EBF4C01A7155D51F39D1FD600F33`이다.
서버와 클라이언트 두 프로세스에서 자기 실행 사본의 DLL 경로·SHA를 직접 확인했다.
Source 사본 해시는 당시 관측 cohort이며 그 파일들이 해당 DLL로 컴파일됐다는 별도
주장은 아니다. 원본 정상 모듈이 이후 변경돼도 이 결과를 새 모듈의 통과로 확대하지 않는다.

- 실제 개발 인증/Join Ticket 입장·HTTP/PostgreSQL/Iris와 작성된 가방 WBP에서
  hit-test/routed 우클릭을 사용했다. 직접 UI 함수/RPC 제출·projection 주입은 없다.
- 최초 소비는 DB에 한 번 커밋됐으나 응답이 유실돼 가방은 옛 7개/revision1과 잠금을 유지했다.
  기존 복구 버튼의 원래 요청/바이트를 재전송하고 정확한 원래 응답을 잠시 보류했다.
- 서버 승인 피해로 원래 Pawn의 HP 120→0을 만들었다(`DirectHPWrite=0`). 클라이언트의
  `DEAD_WAIT`에서 HP0·Pending/옛 projection/원래 Pawn 유지 후에만 응답을 해제했다.
- 같은 request/item의 `CONSUME_DEATH_PASS`에서 수량6/revision2·HP0·Unlocked1을 확인했다.
  서버 `DEATH_RESOLVED`/`DEATH_STOPPED` 모두 Effects0/SamePawn1이다. 부활·포션 회복·
  환불·새 명령은 없고 소비 영수증1개/불변 SHA·추가 커밋/증분 없음, 시작 아이템 보존도 확인했다.
- backend 빌드/실행과 UE/proxy는 자기 사본을 사용했다. 최종 `Passed=true`, 입력 사본 불변,
  종료한 로그의 runtime diagnostics0·임시 정리 완료다. 기존 native/프록시 suite는 반복하지 않았다.

최종 실행은 `65a5827f8e66408d859b6b1812c0c9ef/E2E-0c940145713244dda57737aed87142f5/result.json`,
모든 경로는 `Saved/BuildChecks/ConsumeDeathRuntime-20261101/` 아래다.
요약은 `Summary.proof.json`, 실제 로드 관측은 최종 실행의 `LoadedModules.json`이다.
초기 두 실행은 junction 아래 WBP 탐색 실패(DB/UE 전), 사본의 기존 economy seeder 누락
(격리 backend 빌드/준비 성공·자기 캐릭터/UE 전)으로 구분해 보존했다. 실제 호출에 필요한
기존 seeder/출발 helper와 WBP overlay만 포함했고 원본 실행기/제품 정책은 수정하지 않았다.

자기 자식 종료·파일 사용 해제와 절대 경계/reparse 미추적을 확인한 뒤 세 번의 backend
출력/실행 프로젝트/DLL·캐시·UserDir **219,351,891 bytes(209.19MiB)**를 모두 자동 폐기했다.
잔여 임시 프로젝트/자기 포트·정리 예외0, 기존 DB fixture는 원래 계약대로 보존한다.
원본 소스·정상 Binaries/Intermediate·공유 DDC/기존 복구 백업과 타 담당 프로세스는 유지했다.
최종 진단 로그의 Join Ticket은 종료 후 마스킹했다. `LogRedaction.proof.json`에 원문/보존본
SHA를 나누어 기록하며 E2E의 `ClosedLogs` 해시는 폐기 전 원문 기준이다. 폐기한 DLL/로그
해시는 당시 증거일 뿐 동일 사본의 재실행/재감사가 가능하다는 뜻이 아니다.

이는 한 로컬 클라이언트의 같은 서버/원래 Pawn에서 사망 후 원래 성공을 회복 없이 종료하는
경로의 확인이다. 실제 Steam/다중 PC·물리 입력·서버 재기동 후 효과 복구/Shipping 또는
전체 게임 검증 완료를 의미하지 않는다. 본인 실행/스크립트·문서 예약을 모두 해제한다.

### 소비 응답 유실 주입 도구 준비 (2026-10-27, 프록시 검사 통과·UE 실연결 전)

기존 `InventoryRecoveryHttpProxy.mjs`에 같은 세션의 `Item.Consume`을 추가했다.
한 소유자의 지정 포션 ID·서비스 자격 해시에만 동작하며 7→6개/처리량 1·rev 1→2·
가방 0번 칸의 단일 활성 비잠금 mutation으로 제한한다. 정의 ID/version과 원래 아이템/
revision·유효한 effectScope가 있는 지문을 검사하고, 최초 전달 뒤에는 실행 범위·요청
번호·메타데이터까지 같은 frozen 바이트만 허용한다. 이 문자열 검사 자체는 서버의
효과 실행 권한이나 세대 증명이 아니라 제한된 오류 주입 fixture의 범위 검사다.

동일 실제 HTTP 요청의 정상 커밋 응답만 유실시키고, 해제 전 재요청은 upstream으로
전달하지 않으며, 해제 후 원래 응답/전체 레코드가 일치하는 AlreadyCommitted만 전달한다.
다른 소유자 조회·자격·추가 mutation/수량·새 범위와 누락/변조·비200 성공 모양 응답을
차단한다. 외부 서비스·새 API/운영 C++·WBP·재빌드는 추가하지 않았다.

새 소비 6개와 기존 32개, **38/38 Node 검사·exit 0**, 두 mjs 구문과 공백 검사를 통과했다.
후속 원문 전체 일치 검사로 마지막 줄바꿈 지문도 거절했다. 최종 증거는
`Saved/Tests/ConsumeRecoveryProxy-mvphw517/Verification.proof.json`과
`proxy-tests-final.tap`이다. 이는 loopback HTTP와 모의 upstream이며 UE 입력/GAS 효과/
PostgreSQL 실통합의 PASS가 아니다. 당시 미연결이던 기존 러너의 Consume 선택·UI probe와
서버의 원래 Pawn 효과 관측은 아래 실연결에서 완료했다. 소비의 서버 재기동·효과 영속
복구를 허용하는 변경은 아니다.

### 소비의 가방·퀵슬롯 실연결 (2026-10-27, HTTP/PostgreSQL/Iris/GAS 통과)

기존 `Run-DungeonBossReturnE2ETest.ps1 -InventoryRecovery -InventoryRecoveryOperation Consume
-ClientCount 1`에 같은 서버 내 소비를 연결했다. `-InventoryRecoveryConsumeQuickSlot`을
추가하면 가방 우클릭 대신 실제 HUD 퀵슬롯을 좌클릭한다. Consume은 재시작/회수/만료/
다른 복구 실패 모드 혼합과 다중 클라이언트를 실행 전에 거절한다.

- 두 경로 모두 실제 인증·입장 ticket·HTTP/PostgreSQL/Iris와 기존 아이템/공유 ViewModel을
  사용한다. 입력은 hit-test를 통과하는 routed Slate이며 직접 RPC/클릭 이벤트 실행이나
  Projection 주입이 아니다. 퀵슬롯 등록만 기존 `SetItemSlotById`를 이용한 클라이언트 로컬
  준비다. 이번 실행을 드래그 등록이나 키보드/실물 마우스 검증으로 계산하지 않는다.
- 서버 관측은 non-Shipping 전용 플래그로만 동작한다. 현재 인증된 원래 owner/Pawn/ASC와
  신원·연결 세대를 고정하고, 기존 승인 피해 경로로 체력에 여유를 만든다. 직접 HP 쓰기나
  회복 효과 주입 없이 실제 포션 GE delegate와 체력 복제를 관측한다. 전투 gate는 열지 않는다.
- 서버 READY 뒤 생성되는 실행별 scope 파일은 source/owner만 전달하고 크기·필드/GUID를
  검사한다. 현재 인증·아이템 소유권과 별도로 대조하며 이 파일이 권한을 부여하지 않는다.
  최초 실행의 빈 scope 전달 실패를 수정한 것으로, 운영 인증/소비/효과 규칙은 바꾸지 않았다.
- 각 실행에서 최초 커밋 응답 1개만 유실시켰다. DB는 같은 ItemId의 7→6개/rev 1→2·소비
  영수증 1개인 동안 UI는 기존 수량 7/rev 1·체력 135·잠금을 유지했다. 창을 재생성해도
  기존 복구 버튼/원래 요청이 남고, 버튼 클릭 후 같은 바이트의 `AlreadyApplied` 재확인으로
  원래 Pawn/ASC에 효과 1회·체력 135→150·가방/퀵슬롯 수량 6 표시와 잠금 해소를 확인했다.
  전체 레코드 영수증/원래 DB 행 해시·전체 가방/기존 아이템과 `0|0|1` 명령 수가 보존됐다.

가방 `82366e77873f488a9d57a131815a52db`, 퀵슬롯 `b169cffca74d48a8a7ed7fac817c8381`은
각각 새 실행/러너 exit 0이다. 같은 모듈 `6E728DF1…708E66`, 실행별 선택 입력 60개(중복 0)/
모듈이 불변이며 종료 후 backend를 포함한 로그 6개씩 Error/Fatal/Assert/Ensure 0이다.
첫 가방 실행 `e0e5cc907b574781969abca046e6d35a`는 소비 전 scope 누락으로 실패한 그대로
보존했다. 수정 소스 컴파일 뒤 DLL 사용 중 링크 실패가 있었고, 이후 다른 빌드가 공유
모듈을 반영한 상태에서 자체 정상 Editor 타깃 exit 0/Up to date(0개 작업)를 확인했다.
이를 자체 새 링크 성공으로 계산하지 않는다.

증거: `Saved/Tests/ConsumeRecoveryLive-a1539f80/Verification.proof.json`과 각 실행의
`result.json`/`source-before.json`·닫힌 로그. 준비된 프록시를 재사용했으며 완료된 native/
Node·가방/우편/장비 재시작 E2E는 반복하지 않았다. 운영 C++/BP 에셋·API/스키마와 권한/
CAS/잠금을 변경하지 않았다. 서버 재시작/EndPlay·Pawn 교체를 넘는 효과 복원, 자연 전투,
다중 PC/Steam·Shipping·물리 입력이나 영속 DB-GAS 원자성의 증명이 아니다. 이 소비의
필수 실연결은 완료하며 고급 재시작 복구를 다음 콘텐츠 작업의 선행조건으로 만들지 않는다.

## 장비 Reconciliation

Reconciler는 매번 현재 owner의 전체 authoritative record snapshot을 입력으로 받는다.

- Active `Equipment` record만 원하는 상태로 선택한다.
- foreign owner, 잘못된 수량, 중복 ItemId/slot, Definition version 불일치,
  호환되지 않는 slot과 비어 있는 AbilitySet 참조를 전체 적용 전에 거부한다.
- 사라지거나 교체된 장비의 비동기 로드를 취소하고 Actor를 파괴하며 부여했던
  AbilitySet handle만 회수한다.
- 같은 ItemId와 Definition version은 다시 부여하지 않고 Revision만 갱신한다.
- 새 장비는 AbilitySet을 한 번 부여하고 soft actor class를 비동기 로드한다.
- 로드 완료 시에도 현재 slot과 ItemId가 일치할 때만 Actor를 생성한다.
- 생성 Actor는 서버에서 복제를 활성화하고 캐릭터 Mesh의 `AttachSocket`에
  `EquippedActorRelativeTransform`으로 부착한다.

Legacy `URPGItemBase`와 기존 장비 컴포넌트의 타입은 아직 자산/호환 함수 참조를 위해
유지한다. 새 권위 경로는 그 객체를 영속 상태나 판정 근거로 사용하지 않는다.
`URPGInventoryComponent`는 별도 목록·복제·UI·저장을 제거한 호환 파사드이며,
새 Projection과 병행하는 두 번째 인벤토리는 없다. 미이전 저장 데이터는 자동으로
새 아이템을 발급하지 않고 별도 데이터 마이그레이션이 필요하다.

## 스킬·무기 타격 검사의 장비 조회 전환

`FRPGEquipmentSnapshot`은 검증된 native Equipment Record와 Definition으로 만든
읽기 모델이다. ItemId, Definition/version, int64 Revision, 슬롯, 저장된 스탯,
무기/손/유틸리티 종류 및 장착 Actor 참조를 제공한다. 새 영속 목록이나 변경 명령은 아니다.
`URPGAuthoritativeEquipmentComponent`는 성공한 Reconcile 뒤 이 모델을 갱신하고,
비동기 Actor 로드가 끝나면 같은 ItemId의 Actor 참조만 갱신한다.

- Snapshot 배열과 활성 장비 상태는 `COND_OwnerOnly`로 복제한다. 다른 플레이어에게
  장비 스탯 전체를 공개하지 않는다. 기존 외형 Actor 복제는 유지한다.
- `URPGGameplayAbility_Equipment`와 `URPGAnimNotifyState_PerformTrace`는 구형
  Equip/Equipment 목록과 Manifest를 판정에 사용하지 않는다.
- Armor는 native 슬롯, Weapon/Utility는 native Equipment Fragment의 종류 정보를
  읽는다. `WeaponType`, `WeaponHandType`, `UtilityType`의 Count는 아직 미작성된
  정보이며 임의의 종류로 채우지 않는다. 슬롯에 맞지 않는 종류는 정책 등록·Snapshot
  생성 단계에서 거부한다.
- 확정된 native 무기가 있으나 Actor 로딩 중, 요구 손 불일치, 비활성 무기 세트,
  None/Utility 상태라면 시작 무기 Actor로 우회하지 않는다.
- 아직 native 무기 Record가 없는 기존 시작 장비는 PawnCombat의 Actor-only 호환
  경로를 사용한다. 명시된 Actor 종류가 틀리면 거부하며, 호환 허용 플래그가 꺼지면
  이 경로를 사용하지 않는다.
- 스킬은 Blueprint 활성화 이벤트 전에 해당 실행의 Snapshot을 캡처한다. 이후 장비
  스탯 변경은 이미 실행 중인 스킬의 값에 소급 적용되지 않는다.
- `GetEquipmentItemInstance`는 Blueprint 함수 서명만 보존하며 null을 반환한다.
  `GetEquipmentItemId`와 `GetEquipmentStatValueByItemId`를 사용한다. 기존
  Actor 기반 `GetEquipmentStatValue`는 캡처된 native 스탯을 읽는다. Actor가 없는
  Armor의 ID는 컴포넌트의 `FindSnapshotInSlot`으로 조회할 수 있다.
- 기존 `ChangeEquipState`는 새 활성 상태로 전달한다. 이후 구형 장비 실행 경로를
  아래 호환 파사드로 정리했으며 기본 컴포넌트·반영 타입·자산 참조는 아직 유지한다.

자동화 테스트 `ProjectRPG.Item.EquipmentSnapshot` 4개는 Snapshot 검증/스탯 유지,
실제 Helm Definition의 Reconcile·Revision 갱신·소유자 전환, owner-only 복제 설정,
단일 테스트 월드에서의 GAS 활성화와 실행 스탯 캡처, 활성 무기 선택 및 우회 차단을
검증한다. 실제 멀티클라이언트 복제·충돌/피해·콘텐츠 플레이 검증은 별도 단계다.

## 구형 장비 실행 경로 제거 (2026-10-05)

`URPGEquipComponent`와 `URPGEquipmentComponent`는 기본 컴포넌트 이름과 기존
Blueprint 함수 서명을 보존하는 호환 파사드다. 독립적인 장비 시스템을 병행하지 않는다.

- 컴포넌트의 `EquipList`, `EquipmentList`, 별도 `CurrentEquipState`를 제거했다.
  구형 아이템 서브오브젝트 복제, Fragment 즉시 효과 적용/제거, 구형 Actor 파괴,
  GAS handle 회수 경로도 제거했다. 두 파사드 자체는 복제하지 않는다.
- 기존 `EquipItem`은 현재 소유된 Pawn의 PlayerController에서 서버 Record를 조회한다.
  구형 객체의 ID가 Active Inventory Record에 있을 때만 그 Record의 Revision으로
  `ServerEquipItem`을 요청한다. 수량·Manifest·스탯은 신뢰하지 않으며 점유 슬롯의
  아이템을 먼저 해제하거나 교체하지 않는다.
- 기존 `UnequipItem`은 native 슬롯 Snapshot의 ID가 현재 Equipment Record와 일치할
  때만 가방 0~79의 첫 빈 슬롯으로 `ServerUnequipItem`을 요청한다. 가방이 가득 찼거나
  미이전 ID/슬롯, 미소유 Pawn이면 상태를 변경하지 않는다. 인증·보안·정의 정책·
  Revision/슬롯 충돌 검사는 기존 native command/backend 경로에서 수행한다.
- `AddEquipment_Internal`/`RemoveEquipment_Internal`은 raw legacy 쓰기를 거부한다.
  `CanAddEquipment`는 0, 구형 객체 getter는 null, `GetAllEntries`는 빈 배열이다.
  구형 장비 변경 delegate로 저장되지 않은 상태 변경을 알리지 않는다.
- Actor/장착 수량/활성 상태 getter는 native Snapshot과 활성 상태를 읽는다.
  `FindEquippedWeapon`의 구형 Item 포인터는 항상 null이며 로딩 중인 native Actor를
  다른 무기로 대체하지 않는다. 이 호환 getter로 구형 mutable 아이템을 만들지 않는다.
- 활성 상태의 원본은 native 컴포넌트 하나다. 서버 변경과 owner-client RepNotify가
  같은 delegate를 내보내고, 파사드는 기존 `OnEquipStateChanged`에 전달한다.
  동일/잘못된 상태는 알림을 만들지 않으며 등록 해제 시 delegate 연결을 해제한다.
- 기존 reflected Entry/List 타입과 `EquipComponent`/`EquipmentComponent` 기본
  서브오브젝트는 자산 호환을 위해 유지한다. 클래스·자산·DB 삭제나 자동 데이터
  마이그레이션은 하지 않았다. 구형 타입을 완전히 제거한 단계는 아니다.

`ProjectRPG.Item.LegacyEquipment` 테스트 3개는 별도 목록/복제 상태 부재, raw 쓰기의
Record 불변, canonical ID의 명령 전달 및 미이전/가방 용량/미소유 Pawn 거부,
native 상태 읽기와 delegate 등록·해제를 검증한다. 전달 테스트의 서버에는 인증·
backend가 없어 native 명령이 거부되며, 실제 장착/해제 성공을 주장하지 않는다.
RepNotify는 단일 테스트 월드에서 직접 호출하며 실제 네트워크 검증은 아니다.

## 전투 조회와 무기 판정 연결

`UPawnCombatComponent::GetCharacterCurrentEquippedWeapon`도 native 활성 무기
Snapshot을 읽는다. native 무기가 있으면 로딩 중/Actor 소실/None·Utility/비활성 세트에서
시작 무기를 대신 반환하지 않는다. native 무기가 없는 NPC·기존 시작 장비만 actor-only
호환 경로를 유지한다. 구형 태그별 actor getter는 시작 장비 그래프 호환용이며,
새 장비를 구형 태그 맵에 다시 등록하거나 영속 아이템 목록을 재구성하지 않는다.

- 플레이어 기본 공격력 getter는 현재 actor의 기존 `WeaponBaseDamage` 곡선을 평가한다.
  무기 없음·비플레이어 무기·잘못된 레벨/결과에서는 0을 반환하고 null을 역참조하지 않는다.
  아이템 StatTag를 임의로 피해에 매핑하거나 기존 공격력 곡선을 변경하지 않았다.
- 무기 충돌은 서버의 명시적 attack window에서만 켠다. native actor도 해당 시점에
  타격 delegate를 연결하며, callback은 원래 무기·현재 무기·권한·충돌 상태를 검증한다.
  이전 무기에서 대기 중이던 callback이 다음 무기의 타격으로 처리되지 않는다.
- combat은 켜 놓은 actor를 weak 참조로 추적한다. 세트 전환, Snapshot 교체/소실,
  컴포넌트 등록 해제 시 이전 actor의 충돌·delegate·중복 타격 캐시를 정리한다.
  전환만으로 새 무기의 공격 판정을 켜지 않으며 같은 actor의 조회 갱신은 진행 중인
  attack window를 다시 시작하지 않는다.
- native 컴포넌트의 조회 변경 알림은 Reconcile/비동기 actor 로딩/조회 초기화 및
  owner-client Snapshot RepNotify에서 발생한다. 기존 활성 상태 알림과 함께 읽기
  소비자가 구독하며 별도 장착 상태를 복제하지 않는다.

`PlayerCombatComponent.cpp`는 기존 CP949 문자를 왕복 검증 후 UTF-8로 변환했다.
마나 회복 로직은 변경하지 않았다. 이번 단계는 단일 현재 무기의 조회·판정 연결이며,
양손 슬롯 예약/쌍수 공격 선택, 입력·애니메이션의 전체 이관, 클라이언트 타격 연출이나
실제 피해·네트워크 통합 검증을 완료한 단계는 아니다.

## 기존 시작 무기 콘텐츠 감사

`AuditPlayerWeaponContent.py`는 기본 객체와 상속된 Blueprint 그래프를 읽기만 한다.
실제 `DA_Player`가 `GA_Spawn_GreatSword`를 부여하고, 이 능력은 `BP_PlayerWeapon`을
`Player.Weapon.GreatSword` 태그로 `WeaponBackSocket`에 생성한다. 생성 시 활성 무기
등록은 false다. 기존 equip/unequip 그래프는 `WeaponHandSocket`/`WeaponBackSocket`,
`IMC_GreatSword`, `AnimLayer_GreatSword` 및 actor의 능력 배열을 직접 사용한다.
기본 공격력은 `CT_PlayerWeaponStats`의 `Player.GreatSword.BaseDamage` 곡선이다.

해당 actor의 엄격한 `WeaponType`/`WeaponHandType`은 모두 미지정 Count였다.
이 감사에서는 에셋을 저장하거나 native 무기 Definition·AbilitySet·영속 시작 아이템을
생성하지 않았다. 다음 콘텐츠 이관은 슬롯/손 메타데이터와 기존 입력·애니메이션·GAS
부여 경로를 함께 정리해야 한다. 기존 equip 그래프와 새 AbilitySet을 그대로 동시에
실행하면 이중 부여와 구형 actor 조작이 남으므로 Definition만 연결해 완료로 보지 않는다.

## 플레이어 무기 로드아웃 연결 (2026-10-05)

`URPGPlayerWeaponLoadoutComponent`는 장착 Record를 수정하지 않고, 확정된 장비 조회와
활성 세트에서 선택한 무기의 실행 자원만 관리한다. `ARPGPlayer`의 기본 컴포넌트다.

- 서버에서 actor의 기존 Default/Special 능력 배열을 한 번만 부여한다. 스킬 아이콘,
  쿨다운 태그, 기본 공격력 곡선과 능력 클래스는 원본을 유지한다. 레벨은 현재 startup
  `InputTag.EquipWeapon` spec에서 가져오며 새 밸런스 값을 만들지 않는다.
- 중복 입력 태그나 이미 다른 능력이 점유한 태그는 전체 batch를 부여하기 전에 거부한다.
  자기 spec handle만 회수하며, 시작 무기의 구형 handle도 source와 authored class가
  일치할 때만 정리한다. 다른 실행 자원을 임의로 삭제하지 않는다.
- Stow, 세트/actor/아이템 변경, 로딩 대기, 컴포넌트 해제에서는 이전 능력을 회수한다.
  같은 actor/아이템의 조회 갱신은 재부여하지 않는다. native 무기가 있으면 시작 actor를
  숨기며, 마지막 native 무기가 없어지면 원래 가시성을 복원한다. 구형 능력은 자동 재지급하지 않는다.
- 공개 복제는 native 관리 여부와 활성 actor 참조 두 필드뿐이다. 개인 GUID·Revision·스탯은
  공개하지 않고 기존 owner-only 장비 조회에 남긴다. 입력은 owning local player만,
  animation layer는 해당 pawn의 mesh에서 연결한다. 서버 권한은 클라이언트에서 만들지 않는다.
- 입력 context의 이전 우선순위를 보존하고 자기 우선순위일 때만 제거/복원한다. 다른 context와
  나중에 바뀐 우선순위를 유지한다. PawnClientRestart, PlayerInput 교체, animation 초기화와
  재등록을 처리하며 Tick으로 재부여하지 않는다. 동일 context/동일 우선순위의 다른 작성자까지
  구분하는 범용 참조 카운팅 시스템은 아니다.

기존 `GA_Player_EquipGreatSword`와 `GA_Player_UnequipGreatSword` 두 Blueprint의 부모를
`URPGGameplayAbility_WeaponStance`로 바꿨다. native 무기에서는 서버가 활성 상태만 전환하고
구형 Blueprint의 actor 부착·능력 부여·입력 변경 그래프를 실행하지 않는다. native 무기가 없는
시작 actor 호환 경로에서는 기존 그래프를 유지한다. 기존 LocalPredicted 실행 정책을 유지하고
native 종료는 서버 권한으로 제한한다.

새 카탈로그 에셋은 `/Game/Blueprints/GameData/Item/Weapons/DA_Item_GreatSword`다.
기존 `BP_PlayerWeapon` actor, `WeaponHandSocket`, `IMC_GreatSword`, `AnimLayer_GreatSword`와
능력 9개를 재사용한다. 플레이어 socket의 `hand_r`을 확인해 Primary_R/Secondary_R 슬롯을
지정했다. 종류는 명시된 GreatSword 태그에 맞췄지만 손 메타데이터는 미지정 Count를 유지한다.
별도 영속 AbilitySet에서 입력 능력을 중복 부여하지 않는다. AssetManager 검색/cook 범위는
Item 상위 폴더로 확장해 새 중립 경로와 기존 V2 폴더 에셋을 함께 찾는다.

`MigratePlayerGreatSwordLoadout.py`는 기본적으로 읽기 전용 사전 검사/데이터 검증만 실행한다.
명시적 `-RPGApplyGreatSwordMigration`에서만 위 두 Blueprint와 새 Definition을 저장한다.
이관 전 두 Blueprint를 `Saved/Backups/WeaponLoadout-20261005-871522992d744d91996f8735eeb8a8aa`
에 복사했다. 시작 무기를 영속 아이템으로 자동 변환하거나 DB에 지급하지 않았다.

남은 범위: native 전환은 현재 즉시 상태 변경/가시성 전환이다. 기존 draw/stow montage의
notify 시점·등 소켓 연출, 양손 슬롯 예약/쌍수 선택, 실제 멀티클라이언트 입력·애니메이션,
backend를 통한 대검 획득/장착 성공은 별도 검증이 필요하다. 현재 테스트 결과로 완료를 주장하지 않는다.

## 신규 캐릭터의 영속 시작 무기 (2026-10-18)

위 2026-10-05 이관과 별개로, 이제 **새로 생성하는 캐릭터**에는 백엔드가
`RPGItemDefinition:GameItem.Equipment.Weapon.GreatSword` 버전 1 레코드를 하나 지급한다.
소유자는 캐릭터 GUID, 위치는 `Equipment`의 `Weapon_Primary_R`(슬롯 9), 수량/리비전은
각각 1이며 `CharacterBound`와 `CharacterCreation` 출처를 기록한다. 기존 캐릭터를
소급 변환하거나 구형 시작 actor를 DB 아이템으로 중복 지급하지 않는다.

PostgreSQL에서는 캐릭터 행과 아이템 행을 동일 트랜잭션으로 커밋한다. 개발용 Memory
호스트도 공유 item transaction gate 안에서 이름/roster 확인 뒤 아이템을 기록하고 캐릭터를
공개한다. 이름 중복이면 둘 다 만들지 않는다. 전용 서버의 기존 소유자 아이템 초기 조회와
장비 Reconcile/무기 로드아웃 경로가 이 레코드를 소비하며, native 장비가 있으면 구형 시작
actor는 로드아웃에서 숨긴다.

집중 검증: 백엔드 Release 빌드 경고/오류 0, `ProjectRpg.Backend.StarterEquipmentTests`
메모리 생성·장착 위치·중복 이름 검사가 통과했다. PostgreSQL 실제 트랜잭션, UE 인증 입장 후
스냅샷/공격 입력/보스 피해까지의 연속 실행은 아직 검증하지 않았다. 기존 캐릭터의 장비
마이그레이션도 별도 작업이다.

## 대검 월드 픽업 콘텐츠 연결 (2026-10-05)

`/Game/Blueprints/Item/Equipment/BP_Pickup_GreatSword`를 `ARPGPickUpBase`의 직접 자식으로
추가했다. 새 영속 인벤토리나 지급 서비스를 만들지 않고 기존 서버 획득 명령과
`URPGPickupGrantComponent`의 claim/receipt 처리에 연결한다.

- 픽업 manifest는 GreatSword 태그와 Equip 분류만 제공한다. 구형 수치 fragment나
  `URPGItemBase` 인스턴스를 만들지 않는다. 서버는 이 태그로 native Definition을 찾아
  1개짜리 non-stackable Record를 계획하고, backend 승인 후 owner Projection에 반영한다.
- `DA_Item_GreatSword`의 `PickupActorClass`와 `PickupMesh`를 연결했다. 새 픽업 생성에는
  이 Definition의 actor class를 사용한다. 구형 manifest의 풀링 스폰 경로를 확장하지 않았다.
- 실제 `BP_PlayerWeapon`의 `/Game/Assets/Weapons/LongSword` 메시와 2배 크기를 재사용했다.
  메시에는 simple collision 1개가 있으며, 픽업은 물리 시뮬레이션 없이 query-only /
  WorldDynamic 충돌을 사용한다. 기존 주변 겹침 검사에서 발견할 수 있고, 이름·획득 안내·
  수량 1은 상호작용 표시 데이터에 설정했다. 별도 아이콘은 아직 선택하지 않았다.
- `ConfigureGreatSwordPickup.py`는 기본적으로 읽기 전용 검증이다. 명시적인
  `-RPGApplyGreatSwordPickup`에서만 새 픽업과 Definition 두 에셋을 저장하며,
  이미 다른 데이터가 설정된 에셋은 덮어쓰지 않는다.
- Definition 변경 전 백업은
  `Saved/Backups/GreatSwordPickup-20261005-328f2720327f4c329432872e320df9d0`다.
  맵 배치, 시작 아이템 지급, 드롭/보상 테이블, gameplay 수치 및 DB Record는 바꾸지 않았다.

이 단계는 배치 가능한 획득 콘텐츠와 단일 테스트 월드의 승인/상태 전환 검증이다.
실제 로그인 세션·backend·멀티클라이언트·UI를 통한 획득/장착 검증이나 드롭 정책 완성이 아니다.

## 에디터에서 남은 작업

코드·콘텐츠 검증은 GUI 에디터를 열지 않고 commandlet으로 수행할 수 있다.
실제 입력·외형·UI·멀티플레이 검증에는 에디터/플레이 환경이 필요하다.

1. 장비용 `URPGItemDefinition`에 Equipment Fragment를 추가한다.
2. `CompatibleSlots`, soft `EquippedActorClass`, `AttachSocket`, 상대 Transform과
   `GrantedAbilitySets`를 지정한다.
3. 소모품 Definition에 Consumable Fragment와 `GameplayEffect`를 지정한다.
4. 장비 Actor의 외형, 충돌, relevancy 정책을 플레이 환경에서 확인한다.
5. 새 Projection UI의 실제 클릭·장비 외형·멀티플레이 동작을 검증한다.
   구형 이동/드롭/분할/정렬 호출은 차단되어 있으므로 필요한 기능은 native 명령으로 연결한다.

## 검증 기록

### 2026-10-06 PIE 입력 검증 시도 — Blueprint 오류 대화상자에서 대기

- 재개 시 MCP의 `IsPIERunning`은 `false`였다. 열린 `testmap`에서 입력 검증용
  `StartPIE`를 요청했지만 플레이 준비 중 다음 애니메이션 Blueprint의 컴파일 실패
  경고가 발생했다: `ABP_Gladiator_AnimLayers_Base`, `ABP_Gladiator_AnimLayers_Bow`,
  `ABP_Gladiator_AnimLayers_OneHandSword`.
- 이후 시작 요청과 Slate 조회가 시간 초과됐고 로그는 플레이 준비 단계에서 멈췄다.
  엔진 `PlayLevel.cpp`는 오류 Blueprint가 있으면 `ShowCompilationErrorsDialog`를
  열어 사용자 선택을 기다린다. 대화상자 확인/취소가 필요한 상태로 판단하며,
  Blueprint별 구체적인 컴파일 원인은 아직 확인하지 않았다.
- 오류를 무시하도록 설정하거나 에디터를 강제 종료하지 않았다. 실제 I키 열기/닫기,
  장비 탭 클릭 및 아이템 명령 검증은 실행하지 못했으므로 통과로 기록하지 않는다.
- 시도 전후 인벤토리 UI·장비 UI·`IMC_Default` 3개 에셋의 SHA-256은 동일하다.
  게임 코드·에셋·DB 변경, 임시 서버 실행, 아이템 지급 요청은 하지 않았다.

근거: `Saved/Logs/Project_RPG.log`의 UTC `2026.10.05-16.43.24` 이후 플레이 시작 로그.

### 2026-10-06 실행 중 에디터 MCP 읽기 전용 확인

- 기본 플러그인 프로필의 GUI 에디터와 로컬 MCP(`/mcp`, 포트 8000)에 연결했다.
  사용자가 실행한 `testmap` PIE를 유지했으며 에디터/플레이를 종료하거나 재시작하지 않았다.
- 실제 `BP_PlayerController_C_0`의 `IA_ShowEquipmentWidget` 설정에 대해
  `GetCurrentKeyForTag`가 `I`를 반환하는 것을 확인했다. 이는 현재 키 매핑 확인이며,
  키 입력을 보내 창이 열리고 닫히는 것까지 검증했다는 뜻은 아니다.
- 컨트롤러의 인벤토리/장비 클래스 참조는 각각 `WBP_InventoryProjection`과
  `WBP_EquipmentProjection`이다. MCP로 두 UMG 트리를 읽어 인벤토리의 장비 영역이
  `Collapsed`이고, 별도 장비 창에 좌우 장착 슬롯 영역이 있는 것을 확인했다.
  두 창의 크기 설정은 각각 720×640, 560×620이다. 실행 중 렌더링 검증과는 구분한다.
- owner Projection의 로딩 상태는 `Uninitialized`였다. 이를 빈 인벤토리 로딩 완료로
  간주하거나, 테스트 Record를 주입하여 `Ready`로 바꾸지 않았다.
- 같은 에디터에서 별도 MCP 클라이언트가 스킬 창을 조작 중이므로 입력 충돌을 피하기 위해
  실제 `I`키 열기/닫기와 탭 클릭 검증은 작업 조율 후 진행한다. 스킬 창을 제거하거나
  캐릭터 클래스를 임의로 선택하지 않았다.
- 이번 확인에서 게임 코드·콘텐츠 에셋·DB는 변경하지 않았고, 아이템 명령·지급·획득·
  장착/해제 요청도 보내지 않았다. 이 기록은 backend/멀티클라이언트 통합 검증이 아니다.

근거: `Saved/Logs/Project_RPG.log`의 `[RPG_INVENTORY_UI]` 기록과 MCP의
`ObjectTools.get_properties`, `UMGToolSet.GetWidgets` 조회 결과.

### 2026-10-05 대검 월드 픽업 콘텐츠 연결

- `Project_RPGEditor Win64 Development` 새 테스트 컴파일·DLL 링크 성공 (exit 0).
- 새 `ProjectRPG.Item.GreatSwordPickup` 2/2 통과: 저장된 native Definition/픽업의 일치,
  실제 WorldDynamic 겹침 검사, 승인 전 미표시와 동시 claim 차단, 승인 후 픽업 종료,
  같은 ID의 장착·해제와 revision 1→2→3, grant replay 및 구형 객체 미생성을 검증했다.
  획득/장비 이동의 저장소는 테스트용 in-memory 경계이며 HTTP/운영 DB를 호출하지 않았다.
- 최종 회귀는 Item 48 + Skill 20, 총 68/68 성공 (exit 0), 자동화 오류 0이다.
- 콘텐츠 저장은 정확히 두 에셋이다. 새 픽업 재로드 후 메모리 컴파일 `BS_UP_TO_DATE`,
  Definition의 엔진 데이터 검증 Valid 1 / Invalid 0, `NoAssetsSaved=1` 확인.
- 변경 전 비교한 기존 에셋 8개 중 Definition만 바뀌었고 나머지 7개는 동일하다.
  최종 검사 전후에는 새 픽업 포함 9개 에셋의 SHA-256이 동일하다.
- 검사에만 `-DisablePlugins=RPGClientUI`를 적용했다. 기본 플러그인 프로필·실제 로그인 세션·
  backend·멀티클라이언트·UI 조작 검증을 대신하지 않는다. 픽업은 맵에 배치하지 않았다.

검증 로그:

- 빌드: `Saved/Logs/GreatSwordPickupBuildFixed-20261005.log`
- 콘텐츠 저장: `Saved/Tests/GreatSwordPickup/Apply/apply.log`
- 콘텐츠 재로드/데이터 검증: `Saved/Tests/GreatSwordPickup/ContentFinal/content.log`
- 집중 테스트: `Saved/Tests/GreatSwordPickup/Focused/automation.log`
- 전체 회귀: `Saved/Tests/GreatSwordPickup/AutomationFinal/automation.log`

### 2026-10-05 플레이어 무기 로드아웃 연결

- `Project_RPGEditor Win64 Development` 빌드·DLL 링크 성공 (exit 0).
  UBA의 선택적 AppData trace 기록에는 권한 경고가 남았지만 컴파일/링크 결과는 Succeeded다.
- 새 `ProjectRPG.Item.WeaponLoadout` 테스트 4/4 통과. 중복 부여·구형 자원 정리·stow·세트 변경·
  재등록·클라이언트 권한 차단·입력 충돌·우선순위 복원·PlayerInput 교체·native stance와 pending
  첫 손의 우회 차단을 단일 테스트 월드에서 검증했다.
- 이관은 정확히 에셋 3개를 저장했고 startup 지급/DB 쓰기는 0이다. 재로드 후 두 stance의
  Primary/None 상태, LocalPredicted 정책과 서버 종료 정책을 확인했다. 새 Definition은
  엔진 데이터 검증에서 Valid 1 / Invalid 0, 재검사는 `NoAssetsSaved=1`이다.
- 최종 전체 회귀는 Item 46 + Skill 20, 총 66/66 성공 (exit 0), 자동화 오류 0이다.
  새 무기 테스트 4개를 포함한다. 관련 Blueprint 13개가 모두 `BS_UP_TO_DATE`로 메모리 컴파일됐다
  (exit 0, `NoAssetsSaved=1`). 이관 후 에셋 17개의 SHA-256이 최종 검사 전후 동일하며,
  이관 전과 비교해 기존 에셋은 의도한 두 Blueprint만 바뀌고 나머지 14개는 그대로다.
- 실제 네트워크·물리 피해·UI 조작 검증을 대신하지 않는다. 검사 실행에만
  `-DisablePlugins=RPGClientUI`를 적용했고 플러그인 설정을 바꾸지 않았다.

검증 로그:

- 빌드: `Saved/Logs/WeaponLoadoutBuildFinal-20261005.log`
- 집중 테스트: `Saved/Tests/WeaponLoadout/Focused/automation.log`
- 콘텐츠 이관: `Saved/Tests/WeaponLoadout/ApplyFixed/apply.log`
- 콘텐츠 재로드/데이터 검증: `Saved/Tests/WeaponLoadout/ContentFinal/content.log`
- 전체 회귀: `Saved/Tests/WeaponLoadout/AutomationFinal/automation.log`
- Blueprint: `Saved/Tests/WeaponLoadout/BlueprintsFinal/blueprints.log`

### 2026-10-05 장비 → 전투 조회·무기 판정 연결

- 최신 `Project_RPGEditor Win64 Development` DLL 링크·빌드 성공 확인.
  병행 빌드 `TargetDataAdmissionBuild.log`에서 보강한 `RPGEquipmentCombatTests.cpp`
  컴파일 및 DLL 링크 성공을 확인했고, 테스트는 그 DLL로 실행했다.
  앞선 캐시 경로 접근 오류는 Makefile 캐시를 백업 이동해 재생성한 뒤 해소됐다.
  자체 재확인 빌드도 Succeeded/exit 0이며, 종료 시 Trace.uba 기록 권한 경고는 있었다.
- `ProjectRPG.Item` 42개와 `ProjectRPG.Skill` 19개, 총 61/61 성공 (exit 0).
  새 `ProjectRPG.Item.EquipmentCombat` 3개는 현재 native 무기/기본 공격력 곡선,
  pending·stow·Actor 소실, 전환 시 이전 충돌·타격 캐시·delegate 정리,
  대기 중인 구형 callback의 원래 source 검증, client 권한 차단과 재등록을 검증한다.
- 관련 플레이어·무기·시작/equip 능력·NPC Blueprint 13개가 메모리 컴파일에서 모두
  `BS_UP_TO_DATE` (exit 0). 실행 전후 SHA-256 13개가 동일하고 자산은 저장하지 않았다.
- 콘텐츠 감사 스크립트도 exit 0, `NoAssetsSaved=1`. 기존 수치·입력·애니메이션 에셋,
  시작 아이템 지급·DB Record는 변경하지 않았다. `git diff --check` 통과.
- 검증 실행에서 `-DisablePlugins=RPGClientUI`만 적용했으며 프로젝트의 플러그인
  설정은 변경하지 않았다. 해당 플러그인을 포함한 기본 프로필 검증은 아니다.
- 앞선 보스까지 포함한 확장 실행은 70/71 성공, `ProjectRPG.Boss.BattleRetry`는
  `Current retry boss` null로 실패했다. 이번 범위에서 보스 코드를 수정하지 않았으며
  원인·해결은 미확정이다. 최신 DLL의 최종 실행은 아이템·스킬만 대상으로 하므로,
  보스 회귀 테스트 전체 성공을 주장하지 않는다.
- 단일 테스트 월드의 조회/서버 판정창과 수동 타격 callback 검증이다.
  실제 물리 overlap 피해·멀티클라이언트 복제·UI 장착/backend 성공이나 native
  무기 콘텐츠/입력/애니메이션 이관 완료를 대신하지 않는다.

검증 로그:

- 최신 전체 빌드: `Saved/Logs/TargetDataAdmissionBuild.log`
- 자체 빌드 확인: `Saved/Logs/EquipmentCombatBuildFresh-20261005.log`
- 최종 테스트: `Saved/Tests/EquipmentCombat/AutomationFinal/automation.log`
- 최종 Blueprint: `Saved/Tests/EquipmentCombat/BlueprintsFinal/blueprints.log`
- 콘텐츠 감사: `Saved/Tests/PlayerWeaponContent/AuditFinal/audit.log`
- 앞선 확장 테스트 (보스 1개 실패 포함): `Saved/Tests/EquipmentCombat/Automation/automation.log`

### 2026-10-05 구형 장비 파사드 정리 — 최종 검증 완료

- 에디터 종료 후 병행 `Project_RPGEditor Win64 Development` 빌드에서 최신
  `UnrealEditor-Project_RPG.dll` 링크와 전체 빌드 성공을 확인했다.
  앞선 DLL 잠금 및 RPGOptionsUI include 오류는 해당 빌드에서 해소됐다.
  이번 장비 작업에서는 옵션 UI 플러그인을 변경하지 않았다.
- `ProjectRPG.Item` 39개와 `ProjectRPG.Skill` 19개, 총 58/58 자동화 테스트 성공
  (exit 0). 새 `ProjectRPG.Item.LegacyEquipment` 3개를 포함한다.
- 관련 Blueprint 15개를 메모리에서 컴파일해 모두 `BS_UP_TO_DATE` 확인
  (exit 0). 대상 자산 15개의 검증 전후 SHA-256은 동일하며 자산은 저장하지 않았다.
- 최초 기본 설정 실행에서는 별도 RPGClientUI 플러그인의 GameFeatureData 누락으로
  초기화 오류가 발생했다. 그 실행에서도 테스트 58개와 Blueprint 15개는 통과했지만
  Blueprint commandlet은 exit 1이었다. 최종 재검증은 실행 인자
  `-DisablePlugins=RPGClientUI`로 해당 플러그인만 제외해 오류 없이 완료했다.
  프로젝트 설정·플러그인·콘텐츠는 변경하지 않았다. 기본 설정의 해당 초기화 문제나
  RPGClientUI 기능이 정상이라는 검증은 아니며 별도 해결이 필요하다.
- `git diff --check` 통과. 검증 범위는 단일 테스트 월드의 장비 조회·명령 전달·
  상태 알림 및 Blueprint 호환이다. 실제 UI 입력·장착/해제의 backend 성공·
  멀티클라이언트 복제·물리 타격 검증을 대신하지 않는다.

검증 로그:

- 빌드: `Saved/Logs/RewardInventoryFirstBuild-ClosedEditor-20261005.log`
- 테스트: `Saved/Tests/LegacyEquipmentRetirement/AutomationIsolated/automation.log`
- Blueprint: `Saved/Tests/LegacyEquipmentRetirement/BlueprintsIsolated/blueprints.log`
- 최초 기본 설정 실행: `Saved/Tests/LegacyEquipmentRetirement/Automation/automation.log`,
  `Saved/Tests/LegacyEquipmentRetirement/Blueprints/blueprints.log`

자산을 저장하지 않는 재검증 스크립트: `Scripts/Editor/ValidateLegacyEquipmentCompatibility.py`.

### 2026-10-04 장비 조회 전환

- UE 5.8.1 `Project_RPGEditor Win64 Development` 빌드 성공.
- `ProjectRPG.Item` 36개와 `ProjectRPG.Skill` 19개, 총 55/55 자동화 테스트 성공.
  새 `ProjectRPG.Item.EquipmentSnapshot` 4개를 포함하며, 마지막 실행은
  None/Utility 상태의 native 무기 조회 차단과 구형 `ChangeEquipState(None)` 전달도 검증했다.
- `ValidateEquipmentSnapshotConsumers.py`로 BP_Player와 기본 공격·장비스킬 Blueprint
  총 6개를 메모리에서 컴파일해 모두 `BS_UP_TO_DATE` 확인.
  자산 저장은 하지 않았으며 대상 6개 파일의 실행 전후 SHA-256이 동일했다.
- 단일 테스트 월드·GAS 및 복제 조건 설정을 확인한 결과다. 실제 멀티클라이언트
  복제, 타격 피해, UI 플레이 결과나 backend 통합 검증을 대신하지 않는다.

검증 로그:

- `Saved/Logs/EquipmentSnapshotBuildStateGuard-20261004.log`
- `Saved/Tests/EquipmentSnapshot/RunStateGuard/automation.log`
- `Saved/Tests/EquipmentSnapshot/Blueprints/blueprints.log`

### 최초 비동기 명령 연결

- UnrealHeaderTool 성공
- 변경 번역 단위 전체 컴파일 성공
- `UnrealEditor-Project_RPG.dll` 링크 성공
- `ProjectRPG.Item` 자동화 테스트 17/17 성공
  - 비동기 Commit 후 Projection/최초 효과 순서 및 로컬 replay 검증
  - 요청과 불일치하는 backend receipt의 Projection/효과 차단 검증

테스트 로그: `Saved/Logs/CodexItemAsyncCommandsAutomationFinalMemoryDDC.log`

### 2026-10-06 분리 인벤토리 창의 중앙 배치 수정

- 실제 MCP `StartPIE` / `PressKey(I)`로 인벤토리 열기를 확인했다. 현재 키 매핑은
  `I`이며, 인증하지 않은 테스트 플레이의 Projection은 `Uninitialized` / 0개다.
  이는 서버에서 조회 완료된 빈 인벤토리라는 뜻이 아니다.
- 플레이 화면에서 창이 왼쪽 위로 밀려 잘리는 문제를 확인했다. UE 5.8의
  `SetPositionInViewport`는 앵커를 `(0, 0)`으로 초기화한다. 인벤토리·장비와
  보상 우편 창 생성 시 위치를 먼저 설정하고 중앙 앵커·정렬을 마지막에 적용하도록
  `RPGPlayerController.cpp`의 호출 순서만 수정했다.
- MCP 콘솔에서 요청한 C++ Live Coding은 `Live coding succeeded`를 기록했다.
  이는 현재 에디터의 패치 반영이며, 에디터 종료 후 수행하는 정식 빌드를 대신하지 않는다.
- 재현 가능한 읽기 전용 검사 `Scripts/Editor/VerifyInventoryWindowPIE.py`를 추가했다.
  중앙 앵커·실제 화면 경계, 활성 창 단일성, 비활성 창 접힘, ViewModel/Projection 상태,
  인벤토리 80칸/10열과 장비 좌우 7칸씩을 검사한다. 에디터 Python 문법 검사는 통과했다.
  스크립트는 키 입력·탭 클릭을 대신하거나 아이템 상태를 만들어내지 않는다.
- 수정 후 새 PIE 시작은 기존 Gladiator 애니메이션 Blueprint 오류 확인 창에서 대기 중이다.
  사용자 요청대로 애니메이션 자산은 수정하거나 저장하지 않았다. 확인 창의 `Play`를 넘긴 뒤
  중앙 배치·탭 클릭·I키 닫기·창 재사용을 검증해야 하며, 아직 그 검증의 성공을 주장하지 않는다.
- 아이템 지급, 장착/해제/우편 수령 명령, DB 변경, 맵 저장은 수행하지 않았다.
  인증된 서버 연동 아이템 조작 검증은 별도 단계로 남아 있다.

### 2026-10-09 분리 창의 실제 PIE 입력 확인

- 새 GUI 에디터의 MCP 서버에 연결해 `testmap` PIE를 시작했다. 이전 중앙 앵커
  수정이 포함된 실행 상태에서 인벤토리와 장비 창이 각각 화면 중앙에 표시되는 것을
  캡처로 확인했다. 이번 단계에서는 게임 C++ 코드를 변경하거나 다시 빌드하지 않았다.
- 실제 MCP `PressKey(I)`로 인벤토리를 열고, 장비 창을 닫고, 다시 인벤토리를 열었다.
  재열기 후 같은 인벤토리 UObject가 활성화되는 것을 확인했다.
- 최초 장비 탭 마우스 클릭으로 별도 장비 창에 전환됐다. 인벤토리 80칸/10열과 장비
  좌우 7칸씩, 중앙 앵커·정렬, 비활성 창의 `Collapsed` 상태를 읽기 전용 검사로 확인했다.
- 재열기 후 장비 버튼의 `OnClicked` 연결과 활성 상태는 정상이고, 해당 버튼에 포커스를
  준 뒤 실제 MCP `PressKey(Enter)`로 장비 창에 전환됐다. 장비 창의 BAG 버튼에도 같은
  방식으로 입력해 인벤토리로 복귀했다. 두 창의 기존 UObject를 재사용했다.
- 반복 MCP 마우스 클릭은 재열기 후 전환되지 않는 경우가 있었다. Hover 뒤 포인터 좌표는
  `(2131, 544)`로, 캡처된 에디터 창 `1920×1032`의 바깥이었다. 게임 버튼 자체의 결함으로
  확정하지 않았으며 사용자의 실제 마우스 전환 확인을 요청했다. 반복 마우스 동작은 미확정이다.
- 검사 스크립트의 Python geometry 읽기는 실제로 보이는 창과 viewport에도 크기 0을 반환했다.
  따라서 잘못된 화면 경계 실패 검사를 제거하고, 배치의 화면 확인은 MCP 캡처로 수행했다.
  이후 콘솔에서 검사 함수를 재사용할 수 있도록 모듈 import 방법도 바로잡았다.
- Projection은 전후 모두 `Uninitialized`, projected/mail 항목 0개였다. 두 ViewModel에
  아이템 명령 결과가 없음을 확인했다. 실제 아이템 사용·획득·장착/해제·우편 수령과 인증된
  서버/멀티클라이언트 왕복 검증은 이번 확인 범위에 포함되지 않는다.
- 중간 `RealTabRoundTrip=1` 로그는 앞선 equipment 검사가 실패한 뒤 bag 검사만 통과해
  출력된 것이므로 왕복 성공 근거로 사용하지 않는다. 최종 상태 확인은 UTC
  `2026.10.08-21.07.48`의 `VERIFIED KeyboardRoundTrip=1 WindowReuse=1
  ProjectionUnchanged=1 NoItemCommands=1` 기록을 기준으로 한다.
- 사용자 확인을 위해 PIE와 인벤토리 창을 유지했다. 에디터/콘텐츠 저장은 요청하지 않았다.

근거 및 캡처:

- `Saved/Logs/Project_RPG.log`의 `[RPG_INVENTORY_UI]` 기록
- `Saved/Tests/InventoryPIE/20261009/Inventory-Final.png`
- `Saved/Tests/InventoryPIE/20261009/Equipment-Tab.png`

### 2026-10-09 상단 바 우클릭 드래그 자동 검증

이 절은 당시 우클릭 구현의 검증 이력이다. 현재 입력은 아래 2026-10-12 좌클릭 전환 절을 따른다.

- 인벤토리와 장비 창의 기존 `TitleText` 행을 드래그 영역으로 사용한다. 상단 행에서
  우클릭을 누른 채 이동하면 창이 이동하고, 버튼 해제·마우스 캡처 상실·창 비활성화 시 종료한다.
  제목 행 밖에서는 창 드래그를 시작하지 않는다. 다른 기존 창은 좌클릭 드래그를 유지한다.
- 드래그 소스가 포함된 일반 Editor 빌드의 성공 기록과 빌드 산출물을 확인한 뒤,
  화면 없는 자동 테스트를 실행했다. 최종 실행은 **2/2 성공, 실패 0, 미실행 0, 프로세스 종료 0**이다.
- 두 실제 Widget Blueprint를 생성하고 Slate geometry/input을 이용해 0.5/1/2 배율 이동,
  반복 이동의 오차 누적 방지, 다른 버튼 해제, 우클릭 해제, 캡처 상실, 누락된 해제 이벤트,
  닫기/재열기 시 드래그 종료 및 공통 창의 기존 좌클릭 드래그를 검사했다.
- 재실행: `Scripts/Tests/Run-InventoryWindowDragAutomationTest.ps1`.
  종료 코드뿐 아니라 두 테스트의 성공 기록과 실패/assert 로그를 검사하며,
  실행 전후 Editor 모듈 SHA256이 같아야 통과한다.
- 최종 근거: `Saved/Tests/InventoryWindowDrag/bba40e555fc544ffa95096a5ea9fdb64/index.json`
  및 같은 디렉터리의 `automation.log`. 검사한 모듈 SHA256은
  `371DE3D26885C0F921AA0812ADDA1D00EF423FB2D2D679FEC4C5CCA366469165`이다.
- 이 검증은 Native Slate 이벤트/배치 계산 검증이다. GPU 화면 품질과 실제 마우스의
  전체 이벤트 전달 경로를 검증한 것은 아니므로, 실제 플레이 드래그는 별도로 확인해야 한다.
  애니메이션 에셋 수정, 아이템 지급/사용/장착/회수, 백엔드·DB 변경은 수행하지 않았다.

### 2026-10-10 제목 바의 화면 밖 이동 방지

- 인벤토리·장비 창을 드래그할 때 제목 바가 해당 플레이어의 화면 영역 안에 남도록
  화면 좌표 이동량을 제한한다. 화면보다 큰 제목 바는 양쪽 끝 정렬 사이에서 이동할 수 있다.
  다른 기존 창에는 이 제한을 적용하지 않으며, 아이템 슬롯 입력과 명령 처리는 변경하지 않았다.
- 드래그 시작 시 제목 바의 경계를 고정해 같은 프레임의 반복 이동에서 오차가 누적되지 않게 했다.
  이동량 제한 후 기존 DPI 배율 보정을 적용한다. 화면 geometry를 얻지 못하면 기존 이동을 유지한다.
- 이번 검사용 빌드는 설정 폴더와 UBA 저장소의 접근 권한 문제로 완료되지 않았다.
  해당 검사 프로세스만 종료했고, 이후 새로 생성된 Editor 모듈로 자동 테스트를 실행했다.
  실행 전후 모듈 SHA256이 동일함을 확인했다.
- 결과: **3/3 성공, 실패 0, 미실행 0, 프로세스 종료 0**.
  기존 두 드래그 테스트와 새 `TitleBarStaysInBounds` 테스트를 검사했다.
  새 테스트는 화면 원점, 상하좌우 경계, 0.5/1/2 배율, 화면보다 큰 제목 바,
  사용할 수 없는 geometry에 대한 순수 경계 계산 검증이다.
- 재실행 스크립트는 세 테스트의 성공 기록을 모두 요구하도록 갱신했다:
  `Scripts/Tests/Run-InventoryWindowDragAutomationTest.ps1`.
- 근거: `Saved/Tests/InventoryWindowDrag/f732353622964584ad32bb3ada7dcfa7/index.json`
  및 같은 디렉터리의 `automation.log`. 검사한 모듈 SHA256:
  `B5FF6F82B412DB0AE2AA6E2D53CBEF3295B5B26721CCFA87769B37A42E22E3A9`.
- 실제 플레이어 viewport에서의 입력 전달과 GPU 표시, 실제 마우스 드래그는 별도 수동 확인이 필요하다.
  에디터에서 인벤토리와 장비 창의 제목 바를 우클릭한 채 화면 각 가장자리로 끌어보고,
  제목 바를 다시 잡을 수 있는지, 버튼 해제 후 이동이 멈추는지 확인한다.

### 2026-10-12 실제 드래그 전후 검사 도구 보강 — 입력 검증은 미완료

- Unreal MCP 서버 `127.0.0.1:8000/mcp`의 초기화·도구 검색·호출을 확인했다.
  공유 에디터에서 다른 작업의 플레이 시작 직후 읽기 요청도 timeout이 발생했고,
  이후 응답이 복구됐다. 다른 작업의 플레이/프로세스는 종료하지 않았다.
- 기존 읽기 전용 `Scripts/Editor/VerifyInventoryWindowPIE.py`에 드래그 전후의
  render translation, 동일 world/controller/window, Projection의 아이템 GUID·리비전·
  수량·위치와 최근 명령 결과를 비교하는 함수를 추가했다. 화면 geometry의 0 크기 값으로
  경계를 판정하지 않는다. 마우스 입력·창 열기·아이템 명령·에셋 저장은 이 스크립트가 수행하지 않는다.
- 모듈 import는 검사를 자동 실행하지 않는다. 파일을 콘솔에서 직접 실행하거나,
  import 후 검사 함수를 명시적으로 호출한다. 이동하지 않은 창을 드래그 성공으로 표시하지 않고,
  버튼 해제 이후 이동, 같은 개수의 아이템 변경, PIE/창 교체도 실패로 처리한다.
- 오프라인 비교 로직 테스트 **10/10 성공**:
  `D:/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe Scripts/Tests/test_inventory_window_pie.py`.
  Unreal을 대체한 모듈에서 검사기의 판정만 검증하며 실제 UI 입력 테스트가 아니다.
- 실제 MCP 콘솔에서 새 코드 import와 native 속성 읽기를 확인했다.
  `Saved/Logs/Project_RPG.log`의 UTC `2026.10.11-15.36.03`에
  `[RPG_INVENTORY_UI] DRAG_BINDINGS PASS translation=(0.0, 0.0) entry_count=1 command_fields=3`이 있다.
  이 검사는 CDO와 기본 구조체의 읽기 바인딩 확인일 뿐, 플레이 창의 이동이나 아이템 지급 증거가 아니다.
- 실제 확인 순서(현재 좌클릭 설정): 안정된 PIE에서 I로 창 열기 → baseline 기록 → 실제 제목 바 좌클릭 드래그 →
  이동 비교 → 새 baseline 기록 → 버튼 없이 포인터 이동/닫기·재열기 → 위치 불변 비교.
  장비 창에서도 반복하고, 화면 가장자리의 제목 바 접근 가능 여부는 실제 화면으로 확인한다.

```python
# UE Python 콘솔에서 import 후 실행한다. 기존 import가 있으면 importlib.reload(inv_ui).
import sys
sys.path.append("C:/UE5/Project_RPG/Scripts/Editor")
import VerifyInventoryWindowPIE as inv_ui
before_drag = inv_ui.capture_inventory_window_drag_pie("inventory")
# 여기서 실제 마우스로 제목 바를 좌클릭한 채 이동하고 버튼을 놓는다.
inv_ui.verify_inventory_window_drag_pie(before_drag, True)
after_release = inv_ui.capture_inventory_window_drag_pie("inventory")
# 여기서 버튼 없이 포인터 이동 또는 I로 닫기/재열기를 한다.
inv_ui.verify_inventory_window_drag_pie(after_release, False, (0, 0))
```

- 게임 C++ 빌드·DLL 교체·기존 자동화 전체 재실행은 하지 않았다. 실제 우클릭 이동, 화면 경계,
  반복 탭 마우스 전환 및 인증된 아이템 UI 조작은 아직 미완료다. 직업 선택·애니메이션은 수정하지 않았다.

### 2026-10-12 제목 바 드래그를 좌클릭으로 전환

- 사용자 요청에 따라 인벤토리·장비 창의 드래그 시작/유지/해제 버튼을 좌클릭으로 변경했다.
  기존 제목 바 영역 제한, DPI 보정, 화면 경계 제한과 캡처 상실/창 닫기 시 취소는 유지한다.
- 아이템 칸 코드는 변경하지 않았다. 가방 아이템의 좌클릭 퀵슬롯 드래그와
  우클릭 사용·장착/장착 해제는 그대로이며, 제목 바 밖의 클릭은 창 드래그를 시작하지 않는다.
- native 테스트와 러너를 `LeftButtonTitleOnly`로 맞췄다. 우클릭 제목 바/좌·우클릭 제목 바 밖 거절,
  좌클릭 이동·해제, 다른 버튼 해제, 캡처 상실, 누락된 해제와 닫기/재열기를 검사한다.
  읽기 전용 PIE 검사 안내도 좌클릭으로 수정했다.
- 오프라인 검사기 **10/10 성공**, PowerShell 러너 구문 검사 성공. 이는 C++/실제 마우스 검증이 아니다.
- 변경된 C++ 두 파일의 `-SingleFile -NoLink` 빌드는 UBA 저장소 접근 거절로 컴파일 전에 중단했다.
  이 작업의 빌드 프로세스만 종료했고, 공유 에디터/DLL은 변경하지 않았다.
  로그: `Saved/Logs/InventoryWindowLeftDrag-NoLink-20261012.log`.
- 다음은 정상 빌드 후 좌클릭 native 테스트 실행과 PIE 제목 바 이동·해제 확인이다.
  이전 우클릭 테스트 통과 기록을 새 좌클릭 구현의 통과 근거로 사용하지 않는다.

### 2026-10-13 좌클릭 드래그 native 및 MCP PIE 검증

- 공유 작업에서 생성된 새 Editor DLL과 두 변경 파일의 컴파일 산출물을 확인했다.
  이번 단계에서 C++ 재빌드나 DLL 교체는 하지 않았으며, 현재 DLL의 좌클릭 동작을 직접 검사했다.
- `Run-InventoryWindowDragAutomationTest.ps1` 결과는 **3/3 성공, 실패 0, 미실행 0, exit 0**이다.
  `LeftButtonTitleOnly`, `ExistingWindowsKeepLeftButton`, `TitleBarStaysInBounds`가 모두 성공했다.
  실행 전후 및 PIE 검증 후 DLL SHA256은
  `8AFC2F3FD3EF393216EA44EDCBB1714DF70F2F5F91E5C7A9B4F987D1CB41D457`로 동일했다.
  근거: `Saved/Tests/InventoryWindowDrag/0fa8b138dcb84bf8acdbb2f662c0f69f/index.json` 및 `automation.log`.
- 같은 DLL을 로드한 GUI 에디터에서 MCP의 Slate 입력으로 다음을 확인했다.
  - I로 인벤토리를 열고 가방 80칸/장비 14칸의 분리 배치를 검사했다.
  - 두 제목 바의 좌클릭 드래그 후 render translation이 실제로 변했고 같은 창/아이템 상태를 유지했다.
    최초 이동량은 장비 `(-532.8047, 585.7408)`, 가방 `(-532.8048, 587.5261)` layout 단위였다.
  - 해제 후 버튼 없는 Hover에서 두 창의 이동량이 `(0, 0)`이었다.
  - 장비↔가방 탭을 3회 왕복하면서 창 두 개만 유지되고 각 창의 위치/아이템 상태가 바뀌지 않았다.
  - I로 닫기/다시 열기 후 같은 가방 창과 위치를 확인했다.
  - 두 창을 우상단/좌하단까지 이동시켜 네 화면 경계에서 제목 바가 남는 것을 스크린샷으로 확인했다.
    화면 끝에서도 제목 바를 다시 잡아 이동할 수 있었다.
- 첫 MCP Drag는 성공 반환값과 달리 창이 이동하지 않아 전후 검사에서 실패했다.
  제목 바 Click으로 포인터를 먼저 맞춘 뒤 Drag하면 두 창 모두 이동했다.
  이 최초 실패와 후속 성공은 로그에 함께 보존하며, 도구 반환값만으로 통과를 판정하지 않는다.
- PIE 증거는 같은 실행 디렉터리의 `PIE/editor.log`와
  `Inventory-Before.png`, `Inventory-After.png`, `Equipment-After.png`,
  `Inventory-TopRight.png`, `Inventory-BottomLeft.png`,
  `Equipment-TopRight.png`, `Equipment-BottomLeft.png`에 있다.
  검증 후 자신이 시작한 PIE와 해당 관찰자만 정리했고 공유 에디터/다른 프로세스/에셋은 유지했다.
- 이는 실제 PIE 화면과 hit-test/capture 경로를 거친 **MCP 합성 Slate 입력** 검증이다.
  OS/실물 마우스 경로 및 인증된 아이템 사용·장착·퀵슬롯 조작의 검증은 아니다.
  당시 Projection은 `Uninitialized`였으므로 로드된 빈 인벤토리로 계산하지 않는다.
  애니메이션/직업 선택 수정, 아이템/백엔드/DB 변경은 하지 않았다.
- 기존 UBA 오류의 원인은 엔진 `ExecutorFactory.GetUBAExecutor`가 `-NoUBA`에서도 UBA를 사용하고
  detouring만 끄기 때문임을 소스에서 확인했다. `UBAExecutor`의 기본 저장 경로는 ProgramData다.
  향후 필요한 빌드에서는 `-UBARootDir=C:\UE5\Project_RPG\Saved\Build\UBA-Inventory`로
  프로젝트 내 저장 경로를 지정할 수 있다. 이 옵션은 소스에서 확인했으며 이번에는 실행하지 않았다.
  전역 설정/권한/엔진 파일은 변경하지 않았다.
