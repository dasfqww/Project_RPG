# Item Async Commands and Authoritative Equipment

## 구현 결과

Dedicated Server의 장착, 장착 해제, 소모 요청을 다음 단일 흐름으로 연결했다.

```text
Owning Client RPC
  -> 서버에서 Character Owner/Container 확정
  -> FRPGItemActionCommandPlanner 검증
  -> Item V2 비동기 Commit
  -> receipt 요청 ID/명령/지문/주체 검증
  -> authoritative Inventory Projection 병합
  -> 장비 Snapshot 기반 Actor/AbilitySet 조정
  -> 최초 Succeeded인 소모 요청만 GameplayEffect 적용
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
- 소모 GameplayEffect는 backend의 최초 `Succeeded`에만 실행하고
  `AlreadyApplied`에는 다시 실행하지 않는다.

프로세스가 Commit 직후 종료되는 경우까지 소모 효과를 정확히 한 번 보장하려면
backend outbox 또는 영속 effect receipt가 필요하다. 현재 메모리 receipt와 pending
queue는 정상 실행 중의 네트워크 재시도 및 Pawn 교체에는 안전하지만 서버 재시작을
넘는 원자성 경계는 아니다.

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

Legacy `URPGItemBase`와 기존 장비 컴포넌트는 UI 전환 기간 동안 병행되지만,
새 권위 경로는 그 객체를 영속 상태나 판정 근거로 사용하지 않는다.

## 에디터에서 남은 작업

코드 검증에는 에디터가 필요하지 않다. 실제 콘텐츠 연결 단계에서만 에디터를 연다.

1. 장비용 `URPGItemDefinition`에 Equipment Fragment를 추가한다.
2. `CompatibleSlots`, soft `EquippedActorClass`, `AttachSocket`, 상대 Transform과
   `GrantedAbilitySets`를 지정한다.
3. 소모품 Definition에 Consumable Fragment와 `GameplayEffect`를 지정한다.
4. 장비 Actor의 외형, 충돌, relevancy 정책을 플레이 환경에서 확인한다.
5. Legacy Inventory UI 버튼을 `URPGItemCommandComponent` RPC와 결과 delegate에
   연결한다.

## 검증

- UnrealHeaderTool 성공
- 변경 번역 단위 전체 컴파일 성공
- `UnrealEditor-Project_RPG.dll` 링크 성공
- `ProjectRPG.Item` 자동화 테스트 17/17 성공
  - 비동기 Commit 후 Projection/최초 효과 순서 및 로컬 replay 검증
  - 요청과 불일치하는 backend receipt의 Projection/효과 차단 검증

테스트 로그: `Saved/Logs/CodexItemAsyncCommandsAutomationFinalMemoryDDC.log`
