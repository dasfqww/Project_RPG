# MMORPG Item API V2

## 목적

기존 `/api/loadInventory`, `/api/saveInventory`는 이전 클라이언트 호환을
위해 유지한다. 새 API는 아이템 하나를 안정 ID와 Revision으로 관리하고,
여러 아이템 변경과 멱등성 영수증을 하나의 DB 트랜잭션에 기록한다.

## 권한

- 조회: Dedicated Server 또는 해당 Character/Account 소유 플레이어
- Terminal 이력 포함 조회: Dedicated Server 전용
- Commit과 영수증 조회: Dedicated Server 전용

Dedicated Server는 할당된 서버 ID와 Dungeon Session에 결합된 단기 토큰을
사용한다. 일반 아이템 Commit은 게임 서버 전용이며, 던전 보상은 아래의 세션 범위
정산 API를 사용한다.

## 로비 장비 강화·이전 — 2026-11-01

Player Bearer로 자기 캐릭터의 좁은 성장 명령을 호출한다. 기존 범용 Commit/영수증 조회의
Dedicated Server 권한은 유지한다. 새 경로는 캐릭터 소유·정확한 장비7종/광석 ID와 버전·
서버 가격/단계·Revision·귀속/잠금/만료를 확인한다. 기존 대검/성장 미설정 아이템은 자동
초기화하지 않는다. 현재 UE 화면/요청과 새 시작 지급·원본 패키지 설치는 후속이다.

`POST /api/characters/{characterId}/equipment/upgrade`:

```json
{
  "requestId": "21f0d537-5bc4-486f-99f6-d512051d8392",
  "ruleVersion": 1,
  "equipment": { "itemId": "11111111-1111-1111-1111-111111111111", "expectedRevision": "4", "expectedLevel": 1 },
  "oreStacks": [ { "itemId": "22222222-2222-2222-2222-222222222222", "expectedRevision": "2" } ]
}
```

`POST /api/characters/{characterId}/equipment/transfer`는 같은 RequestId/RuleVersion과
`source`/`target`을 받는다. 각 항목의 구조는 위 `equipment`와 같고 `oreStacks`는 없다.
원본+k/같은 부위의 대상0을 원본0/대상+k로 전량 이전한다. 강화는 한 단계만2/4/6개,
광석 후보는1~15개·서버 정렬·Inventory/CharacterBound만 사용한다. 예상 Revision은
0보다 크고 Int64.MaxValue보다 작은 **문자열**, 예상 단계와 규칙 버전은 필수 정수다.
가격·전체 Record·서버 토큰을 제출하지 않는다. UTF-8 JSON 본문 상한은16KiB다.

성공은 기존 `ItemCommitResponse` 형식의200 `Committed`/`AlreadyCommitted`다.
`AffectedQuantity`는 강화 광석 비용 또는 이전0이고 `Records`에는 실제 변경된 장비/재료가
담긴다. 성공 재전송은 현재 리스/단계·위치보다 먼저 원래 영수증을 반환하므로, 클라이언트는
그 응답의 옛 Revision으로 더 최신 장비를 덮어쓰지 않는다. RequestId는 기존 아이템 영수증과
같은 전역 공간을 사용하고 같은 ID의 다른 명령/캐릭터는409다. 서버가 전체 명령의
fingerprint를 계산하며 광석 후보의 입력 순서만 정규화한다.

| 상태 | 의미 |
|---|---|
| 400 | 필수 값/UUID·Revision·규칙·중복 ID/요청 형식 오류 |
| 401/403 | 인증 없음 또는 Player/현재 캐릭터 소유 조건 불충족 |
| 404 | 본인 요청 아이템을 찾을 수 없음. 다른 소유 아이템도 이 경로로 노출하지 않음 |
| 409 | 단계/Revision·재료/상태 충돌, `active_dungeon_equipment_locked`, `equipment_lease_busy`, 재전송 충돌 |
| 413/415 | 본문 상한 또는 JSON Content-Type 조건 불충족 |

새 명령은 유효한 던전 리스가 없어야 한다. 소유 확인·성공 사전 복구 뒤 Memory 공유 gate,
PG의 캐릭터/FK·리스/아이템 잠금과 같은 트랜잭션에서 전체 변경/성공 영수증을 확정한다.
리스 행 사용 중의 `equipment_lease_busy`는 전체 롤백된 재시도 가능 충돌이다.
응답 유실·500/취소는 미저장 확정이 아니므로 원래 ID/명령을 유지해 복구한다.
새 처리자가 만드는 응답은 `no-store`다. [구현/집중 검사·남은 연결](../Docs/PLAYER_PROGRESSION_EQUIPMENT_TRANSACTIONS.md#14-2026-11-01-플레이어-api와-리스아이템-원자-저장)을 따른다.

## 로비 장비 제작·분해 — 2026-11-01

실제 backend 소스에 연결한 좁은 Player 명령이다. 기존 강화/이전과 같은 Bearer·캐릭터 소유·리스/영수증 경계를 사용한다. 현재 실행 서비스/UE 클라이언트에는 배포·연결하지 않았다.

`POST /api/characters/{characterId}/equipment/craft`:

```json
{
  "requestId": "11111111-1111-4111-8111-111111111111",
  "ruleVersion": 1,
  "targetDefinitionName": "GameItem.Equipment.Progression.Target.Chest",
  "targetDefinitionVersion": 1,
  "tokenStacks": [{ "itemId": "22222222-2222-4222-8222-222222222222", "expectedRevision": "1" }]
}
```

`RPGItemDefinition`의 Version1인 `GameItem.Equipment.Progression.Target.{EnduranceGreatSword,CirculationGreatSword,Chest,Gloves}` 중 정확한 한 종류만 허용한다. 같은 캐릭터의 가방에 있는 `GameItem.Material.Progression.Token` Version1을 합계8개 차감한다. 후보 스택은1~15개·중복 ID 금지·Revision은 Int64 양수 문자열이며 ID 정렬 순으로 필요한 수량만 소비한다. 결과 ID·가격·새 Record·능력치는 서버가 결정한다. 결과는 수량1/CharacterBound·명시적 `FirstSlice/1/+0`이다.

`POST /api/characters/{characterId}/equipment/dismantle`:

```json
{
  "requestId": "33333333-3333-4333-8333-333333333333",
  "ruleVersion": 1,
  "equipment": { "itemId": "44444444-4444-4444-8444-444444444444", "expectedRevision": "1", "expectedLevel": 0 }
}
```

가방 또는 정확한 장착 슬롯의 위 목표4종/+0만 전량 소비해 CharacterBound 증표1개를 반환한다. 시작 장비·강화 장비·옛 정의·단계 미설정·다른 소유/잠금/만료·메일/창고 입력은 거절한다. 메일에서 장비/재료를 자동 인출하지 않는다.

두 명령은 비용/소비와 지급·Revision snapshot·성공 영수증을 같은 Memory 게이트/PG 트랜잭션으로 저장한다. 지급은 가방 우선(80칸), 남는 칸이 없으면 `ContainerType=Mail`이다. 비용으로 비워진 칸도 즉시 사용한다. 분해 증표는 기존 보상과 같은 호환성 검사로 가방의 증표 스택에 최대999까지 합친다. 잠금·만료/귀속·태그/수치·성장 상태가 다른 스택에는 합치지 않는다. 일반 Commit 권한은 계속 게임 서버 전용이다.

소유 확인 뒤 정확한 성공 영수증을 현재 아이템/리스보다 먼저 반환한다. 서버 생성 결과 ID는 영수증에 저장되며 재전송 때 새 ID를 만들지 않는다. 같은 RequestId의 다른 입력은409, 동일 비용 스택 목록의 순서 변경만 같은 요청이다. 새 요청은 유효 던전 리스가 있으면409이며 PG 리스 잠금 경합은 `equipment_lease_busy`409/전체 롤백이다.

응답은 기존 `ItemCommitResponse`다. 확정/재전송200, 인증 없음401, Player 이외·다른 캐릭터403, 자기 조회에서 찾을 수 없는 아이템404, 잘못된 형식/규칙·중복 ID400, 정책·부족/Revision·리스 충돌409, JSON 이외415/16KiB 초과413이다. 핸들러 응답은 `Cache-Control: no-store`다. 상세 구현/집중 확인은 [장비 계약17절](../Docs/PLAYER_PROGRESSION_EQUIPMENT_TRANSACTIONS.md#17-2026-11-01-증표-선택-제작과-목표-장비-분해)을 따른다.

## 장비 단계의 선택 상태 — 2026-10-31

아이템의 `state`에 다음 선택 객체를 보존한다. 이는 저장 형식 확장이며 새 강화 명령이나
플레이어의 일반 Commit 권한을 추가한 것이 아니다.

```json
{
  "equipmentProgression": {
    "growthBandId": "FirstSlice",
    "ruleVersion": 1,
    "level": 2
  }
}
```

누락/null은 기존의 **성장 미설정** 상태다. 응답/영수증 JSON은 미설정 필드를 생략하며
기존 아이템을 자동으로 +0 장비로 바꾸지 않는다. 객체가 있으면 세 필드가 모두 필요하고,
지원 구간/버전과 정수 0~3단계만 수용한다. 단계가 있는 Active 아이템은 수량 1,
Terminal 이력은 수량 0이며 스택 병합하지 않는다.

Memory와 PostgreSQL은 단계가 있는 기존 아이템을 새 요청의 필드 누락/null로 지우는
변경을 `ValidationFailed`로 거절한다. 한 요청의 다른 mutation과 성공 영수증도 함께
보존한다. 같은 RequestId/선언 fingerprint로 단계를 바꾸거나 빼서 재전송하면 원래 성공
영수증의 전체 값과 대조해 `IdempotencyConflict`다. 이후 장착/이동으로 현재 Record가
달라졌어도 정확한 원래 요청은 원래 영수증을 돌려준다.

PostgreSQL은 nullable `equipment_progression` JSONB 컬럼과 버전 2 마이그레이션을 사용한다.
버전 1 `Database/schema.sql`은 수정하지 않았다. 실제 장비 Definition 허용 목록·로비의
소유/리스 검사·가격 차감/강화·이전 명령은 별도 구현이며, 이 필드가 있다고 일반 아이템의
강화를 승인하지 않는다. 실제 서비스/기존 DB에는 배포하거나 마이그레이션하지 않았다.

실제 mapper/JSON·Memory 5그룹과 자체 tmpfs PostgreSQL의 버전 업그레이드·저장/재조회·
CAS/누락 거절·제약/초기화 반복 6그룹을 확인했다. 실제 HTTP/UE 왕복·운영 DB 검사는
미실행이다. [구현·증거·남은 연결](../Docs/PLAYER_PROGRESSION_EQUIPMENT_TRANSACTIONS.md#11-2026-10-31-backend-단계-보존과-버전2-마이그레이션)을 따른다.

## 일반 Commit 요청의 입력 경계 (2026-10-29)

`POST /api/item-transactions/commit`은 자동 JSON 본문 바인딩 대신 게임 서버 권한을
먼저 검사한다. 미인증/잘못된 bearer는 기존 middleware의 401, 플레이어는 본문 형식/크기
해석 전에 403이다. 이 순서는 큰 플레이어 요청을 JSON 객체로 만드는 일을 막는 것이며
HTTP 서버가 연결 정리를 위해 전송 데이터를 읽지 않는다는 보장은 아니다.

권한 있는 서버도 **인코딩된 본문 최대 1MiB(1,048,576바이트)**만 허용한다.
Content-Length를 먼저 검사하고, 헤더가 없는 청크 전송도 실제 스트림을 상한+1바이트까지만
읽어 초과 시 413으로 거절한다. `application/json`과 기존 `+json` 타입·ASP.NET JSON 옵션/
문자셋 처리를 유지한다. JSON 타입이 아니면 415, 손상/빈/null 본문과 mutation 개수 초과는
400이며 기존 최대 16개·도메인 검증은 유지한다. 큰 본문을 도메인 변환한 뒤 거절하지 않는다.
거절 시 아이템 서비스 커밋을 호출하지 않으며 원래 요청/영수증·CAS/원자성/복구와 BP/RPC는
변경하지 않는다. 준비된 던전 픽업의 별도 128KiB 정책도 유지한다.

기존 테스트 프로젝트의 `--item-ingress-only`로 실제 loopback HTTP/Memory를 집중 확인했다.
권한 우선·고정 길이/청크/다중 바이트 초과·잘못된 JSON/개수·정확한 상한과 `+json`/UTF-16
호환의 16종, 정상 커밋/정확한 재시도·변경 목적칸 409와 원래 아이템/영수증 보존을 통과했다.
다른 replay/PG 필수 모드 혼용 3종은 예상 종료 2다. 분리 Release 빌드 경고/오류 0·러너 종료 0,
선택 입력 94개/검증 DLL·기존 UE DLL/Config/거래 서비스 불변을 확인했다.
근거: `Saved/BuildChecks/ItemCommitIngress-20261029/Verification.proof.json`.
완료한 PostgreSQL·전체 Memory replay·UE 검사는 반복하지 않았으며 이를 새 PG/UE/BP/Iris
성공으로 표시하지 않는다. 전체 DoS 방어나 호스트/프록시의 rate limit·timeout 설정을 대신하는
것은 아니며 실행 중 backend에는 미배포다. 다음 조율된 backend 배포에 포함한다.

## 엔드포인트

```text
GET  /api/items?ownerType=Character&ownerId={uuid}&includeTerminal=false&limit=200
GET  /api/items/{itemId}
GET  /api/item-transactions/{requestId}
POST /api/item-transactions/commit
```

`commit` 요청 예:

```json
{
  "requestId": "032c94bb-0a68-435e-b6d2-c8d384271d72",
  "operation": "MoveItem",
  "commandFingerprint": "move|item-id|inventory-a|1|2",
  "actor": {
    "type": "Character",
    "ownerId": "ebf16a38-923f-445d-906d-9fc0ea8bb5df"
  },
  "affectedQuantity": 0,
  "mutations": [
    {
      "expectedRevision": 4,
      "newRecord": {
        "definitionType": "RPGItemDefinition",
        "definitionName": "Potion.Health.Small",
        "definitionVersion": 1,
        "owner": {
          "type": "Character",
          "ownerId": "ebf16a38-923f-445d-906d-9fc0ea8bb5df"
        },
        "location": {
          "containerType": "Inventory",
          "containerId": "ebf16a38-923f-445d-906d-9fc0ea8bb5df",
          "slotIndex": 2
        },
        "state": {
          "instanceId": "91887703-0355-4bc6-949e-a90e275d262f",
          "generationSeed": 104729,
          "quantity": 10,
          "instanceTags": [],
          "statValues": []
        },
        "revision": 4,
        "lifecycleState": "Active",
        "metadata": {
          "bindState": "Unbound",
          "durability": {
            "current": 0,
            "maximum": 0
          },
          "expiresAtUtc": null,
          "creationSource": "Item.Source.Drop",
          "isLocked": false
        }
      }
    }
  ]
}
```

요청의 `newRecord.revision`은 `expectedRevision`과 같아야 한다. Commit이
성공하면 저장소가 Revision을 1 증가시켜 응답과 영수증에 기록한다.

## 보장하는 불변식

1. 요청에 포함된 모든 Expected Revision을 비교한다.
2. 모든 Mutation을 함께 성공시키거나 전부 롤백한다.
3. 활성 `(Owner, Container, Slot)`에는 아이템 하나만 존재한다.
4. 같은 Request ID와 같은 명령은 원래 영수증을 반환한다.
5. 일반 Commit HTTP 경로에서 같은 Request ID를 다른 지문이나 다른 전체 변경 내용으로 재사용하면 `IdempotencyConflict`다.
6. 완전히 소비된 아이템은 삭제하지 않고 Terminal tombstone으로 남긴다.

### 리비전 증가의 입력 경계 (2026-10-30)

공통 commit 검증은 각 `ExpectedRevision`에 `0 <= value < Int64.MaxValue`를 요구한다.
일반 HTTP는 범위를 벗어난 요청을 400으로, 서비스는 영수증 조회/저장 호출 전에
`InvalidRequest`·빈 records로 거절한다. 여러 mutation 중 하나만 잘못돼도 신규 아이템/
영수증을 일부 저장하지 않는다. 기존 저장소에도 증가 후 레코드 검증이 있어 음수로
넘친 리비전의 저장은 막혔으며, 이번 변경은 실패 경계를 입력 단계로 앞당긴 방어다.

저장된 `Revision == Int64.MaxValue` 레코드의 유효성/조회는 유지한다.
`MaxValue - 1` 요청의 유효성과 마지막 성공 영수증의 정확한 재시도도 보존하며,
리비전 초기화·새 요청/아이템 ID·환불·DB 스키마/UE·BP 변경은 없다.

기존 테스트 프로젝트의 `--item-revision-only`로 domain/service/Memory,
2^53 초과·마지막 리비전의 합성 영수증, 실제 loopback HTTP의 세 그룹을 확인했다.
서비스의 저장소 조회/커밋 0회·혼합 요청 무변경·HTTP 400/정상 CAS 409와 기존 정상
커밋/원래 재시도를 확인했으며 분리 Release 빌드 경고/오류 0·검사 종료 0이다.
다른 모드/PG 필수 옵션 혼용 4종은 예상 종료 2, 입력 95개·검증 DLL 2개·원본 보호
3개의 해시가 불변이다. 기존 HTTP/PG 전체 검사를 반복하지 않았다.
초기 실행 도구의 ExitCode null 수집 실패는 별도 보존하고 프로세스 핸들 고정 후
최종 성공과 구분한다. 두 실행의 Temp 15.67MiB는 종료 후 자동 폐기·잔여/예외 없음이다.
근거: `Saved/BuildChecks/ItemRevisionLimit-20261030/Verification.proof.json`.
실제 PG의 최댓값 행 변경·UE 플레이/정상 모듈·운영 배포 성공은 아니며 기존 backend에도
미배포다. 검증 DLL은 폐기했으므로 해시는 당시 증거이며 재실행에는 재빌드가 필요하다.

### 일반 아이템 재시도의 전체 영수증 대조 (2026-10-29)

`ItemTransactionService`는 기존 영수증을 발견했을 때 작업명/지문/Actor만으로 성공을
인정하지 않는다. Request ID·처리 수량·mutation 개수/순서, 각 아이템의 정의/version·
소유자·컨테이너/칸·ID/seed/수량·태그/스탯과 순서·상태·전체 metadata를 원래 영수증과
대조하며, Revision은 정확한 `Int64 expectedRevision + 1`이어야 한다. 최대값의 덧셈은
거절한다. HTTP 공백/속성 배치가 아닌 파싱된 도메인 필드 기준이다.

다른 내용이면 409 `IdempotencyConflict`와 빈 records를 반환하고 기존 영수증/아이템을
바꾸지 않는다. 최초 조회에는 없었으나 저장소 커밋에서 `AlreadyCommitted`가 반환된
경합도 같은 검사를 거친다. 아이템이 이후 이동/소비됐더라도 현재 아이템 상태가 아닌
**원래 영수증**을 사용하므로 정확한 과거 요청은 원래 기록으로 재확인할 수 있다.
새 요청/아이템 ID·목적칸 재계획이나 환불·추가 지급, DB 스키마/저장소의 CAS·원자성·
보상 재계획 정책 변경은 없다. 이 대조는 일반 HTTP 진입의 서비스 경계이며 내부 저장소
직접 호출 전체의 새 계약으로 확대하지 않는다. 준비된 던전 픽업은 기존 별도 정책을 유지한다.

검증은 기존 `ProjectRpg.Backend.PickupIntentTests`의 `--item-replay-only`로 한정한다.
Memory의 정상/변경 본문 28종·복수 mutation/terminal·후속 이동 뒤 과거 재시도,
조회/커밋 사이의 모의 경합과 합성 영수증의 2^53 초과 Int64/overflow, 실제 loopback
HTTP의 인증/정상 200/다른 목적칸 409를 포함한다. 이 모드는 PostgreSQL을 실행하지
않으므로 `--require-postgres`와의 혼용은 종료 2로 거절한다. 분리 Release 빌드와
5개 집중 그룹은 모두 통과했고 러너 종료 0, 혼용 거절은 예상 종료 2다. 최종 빌드/실행 전후
선택 입력 94개(중복 0)·검증 어셈블리와 기존 UE DLL/Config 3개의 해시가 일치했다.
근거는 `Saved/BuildChecks/ItemReplayIntegrity-20261029/Verification.proof.json`에 기록했다.
UE/BP/Iris·실제 PostgreSQL·운영 배포·Steam/Shipping의 신규 성공 증거는 아니다.

#### PostgreSQL 전용 실행 연결 (2026-10-29)

같은 프로젝트의 `--item-replay-postgres-only`는 기존 사례와 private schema 도구를
재사용한다. 변경 본문 28종·복수 mutation/terminal·과거 재시도·조회 누락 모의 경합,
새 저장소 객체의 JSONB 영수증 재조회/전체 SQL 행·개수 보존과 실제 loopback HTTP를
PostgreSQL 저장소로 실행하도록 연결했다. 모의 경합을 실제 DB 행 잠금 경합으로,
새 저장소 객체를 서버 프로세스 재시작으로 계산하지 않는다.

`PROJECT_RPG_PICKUP_INTENT_TEST_POSTGRES`에 명시된 기존 로컬 개발 DB만 허용한다.
연결 미설정은 종료 2, Memory 전용 옵션과 혼용은 종료 2이며 Memory로 우회하지 않는다.
기존 helper가 host `127.0.0.1`·port `54329`·database/user `project_rpg`를 검사하고
실행별 전용 스키마를 사용한다. `--require-postgres`를 함께 줄 수 있지만 이 모드는
그 옵션이 없어도 DB가 없으면 성공하지 않는다. 본문/토큰·SQL 행 값은 로그에 출력하지 않는다.

분리 Release 빌드 경고/오류 0, 실행 조건 거절 5종을 예상 종료 코드로 확인했다.
선택 입력 94개/운영 서비스 소스·기존 UE DLL/Config는 불변이다. 초기 준비 때 Docker 엔진과
54329 listener가 없어 실제 DB 시도는 `NpgsqlException`/종료 1·PASS 0·스키마 생성 0으로
보존했다. 당시 실DB 5개 그룹은 미완료였으며 이전 Memory 성공을 반복하거나 새 PG 성공으로
승격하지 않았다. 이 초기 준비/실패 근거는
`Saved/BuildChecks/ItemReplayPostgres-20261029/Preparation.proof.json`이다.

##### PostgreSQL 실연결 확인 (2026-10-29, 5개 그룹 통과)

사용자 요청으로 Docker Desktop을 기동했고 기존 `backend-postgres-1`의 healthy/
54329 연결 준비를 확인했다. 입력 94개·검증 DLL 두 개가 준비 당시와 일치해 새 빌드 없이
`--item-replay-postgres-only --require-postgres`만 실행했으며 **5개 그룹 모두 통과·종료 0**이다.
변경 본문 28종 거절/정확한 재시도·복수 mutation/terminal/후속 이동 뒤 원래 영수증 보존,
새 저장소 객체의 JSONB 영수증과 전체 SQL 행/개수 불변, 실제 HTTP/PG 인증과
정상 200/다른 목적칸 409·빈 records를 확인했다. 조회 누락 경합은 모의이며 실제 DB 잠금
경합이나 서버 프로세스 재시작을 검증한 것은 아니다.

검사 전후 입력/검증 DLL·운영 서비스 소스·기존 UE DLL/Config와 이전 실패 기록은 불변이다.
실행별 임시 스키마는 기존 helper가 정리했고 독립 `pg_namespace` 조회의 잔여 0도 확인했다.
public 데이터/기존 컨테이너·볼륨은 삭제하지 않았고 Docker/DB는 계속 켜둔다.
Memory·UE 검사를 반복하거나 게임 코드/BP·공유 모듈/기존 backend를 교체하지 않았다.
실행 근거: `Saved/BuildChecks/ItemReplayPostgres-20261029/Live-mvsar74i/Verification.proof.json`.
이는 로컬 API/PG의 확인이며 UE/BP/Iris 플레이·운영 배포·Steam/Shipping 성공은 아니다.
다음 backend 배포에 기존 구현을 반영하며 완료한 이 검사를 반복하지 않는다.

2026-10-30 자동 폐기 지침에 따라 `ItemReplayIntegrity-20261029`,
`ItemCommitIngress-20261029`, `ItemReplayPostgres-20261029`의 분리 `artifacts/`를
정리했다. 위 성공/실패 기록은 당시 증거이며 보존된 DLL의 재실행이나 고정 입력
재감사가 계속 가능하다는 뜻은 아니다. 기존 proof·실행 명령/소스·모듈 해시/필수 로그와
PG 실연결 결과는 그대로 남겼다. 소스/유지 테스트·기존 backend/DB·컨테이너/볼륨은
변경하지 않았다. 정리 범위·보존 해시 결과:
`Saved/Maintenance/ItemVerificationCleanup-20261030/Result.json`.

## 던전 아이템 보상

던전 클리어 아이템은 일반 Commit을 파티원별로 직접 호출하지 않는다.
`POST /api/dungeon-sessions/{sessionId}/settle-rewards`의 `itemRewards`에 정적 정의,
수량, 귀속, 내구도, 태그와 롤 스탯을 넣는다. 백엔드는 각 파티원에 대해 결정적
Instance ID와 `Mail/DungeonReward.{sessionId}` 위치를 만들고, 파티 전체 재화와
아이템을 같은 트랜잭션에 저장한다. 아이템 ID·위치·검증 충돌이 발생하면 같은 정산의
재화 변경도 전부 롤백된다.

응답 상태와 HTTP 상태:

| 상태 | HTTP |
|---|---:|
| `Committed`, `AlreadyCommitted` | 200 |
| `InvalidRequest`, `ValidationFailed` | 400 |
| `NotFound` | 404 |
| `IdempotencyConflict`, `RevisionConflict`, `LocationConflict` | 409 |
| `InternalError` | 500 |

### 정산 보상의 선택 장비 단계 — 2026-11-01

`settle-rewards`의 각 `itemRewards` 항목에도 다음 선택 객체를 전달할 수 있다.

```json
{
  "definitionType": "RPGItemDefinition",
  "definitionName": "GameItem.Equipment.Progression.Target.EnduranceGreatSword",
  "definitionVersion": 1,
  "quantity": 1,
  "maxStackSize": 1,
  "bindState": "CharacterBound",
  "durabilityCurrent": 0,
  "durabilityMaximum": 0,
  "instanceTags": [],
  "statValues": [],
  "equipmentProgression": { "growthBandId": "FirstSlice", "ruleVersion": 1, "level": 0 }
}
```

객체가 있으면 세 필드가 모두 필요하다. 지원 구간/버전·정수0~3, 정확한 장비7종/DefinitionVersion1·수량1/MaxStackSize1·CharacterBound를 검증하며 잘못된 상태/조합은400이다. 이는 보상 형식의 상태 보존 범위이며 실제 첫 보스 드롭은 수치안의 +0 정책으로 별도 작성해야 한다.

누락/null은 성장 미설정 그대로다. 기존 정산을 새 +0으로 보정하지 않고, null 필드는 직렬화에서 생략해 기존 canonical JSON/fingerprint를 보존한다. 명시적 단계는 fingerprint와 저장된 정산 `item_rewards` JSON에 포함한다. 같은 정산의 단계 변경은 저장소의 정산 충돌로 거절한다. Worker는 그 고정 상태를 지급 Record에 복사하며 기존 가방 우선/초과 우편·파티 전체 원자 저장·영수증 재사용을 유지한다. 새 SQL/테이블은 추가하지 않았다.

권한도 기존과 같다. 정산 제출은 배정된 GameServer만 가능하고 Player/관리자는403이다. 완료 뒤 서버 자격은 기존의 제한된 snapshot GET/감사 범위로 전환돼 같은 POST도403이다. 저장소의 정산 재전송/Worker 재시도 검증을 HTTP의 무제한 재제출 권한으로 해석하지 않는다.

backend 구현과 Memory·실제 backend HTTP/전용 PG 확인을 완료했다. 현재 UE `FRPGDungeonItemReward`/정의·전송 codec에는 이 필드를 연결하지 않았으며 개인별 추첨·원본 에셋/실제 보스 지급·현재 서비스 배포는 후속이다. [실제 구현·검사 범위](../Docs/PLAYER_PROGRESSION_EQUIPMENT_TRANSACTIONS.md#18-2026-11-01-정산-보상의-명시적-장비-단계-보존)를 따른다.

## 저장소 구현

- `InMemoryItemRepository`: 로컬 개발과 API 계약 검증용
- `PostgresItemRepository`: 행 잠금, Revision CAS, 위치 유일 인덱스,
  Request ID advisory lock, 영수증 JSONB를 사용하는 운영 구현
- `item_records`: 권위 있는 아이템 원본
- `item_transaction_receipts`: 재시도 결과와 감사 정보

## 던전 픽업 예약 기록 (2026-10-14)

미저장 픽업 복구의 첫 단계로, 지급 전에 원래 요청을 고정하는 서버 전용 API를
추가했다. **예약 기록은 아이템 지급 영수증이나 재지급 권한이 아니다.** 기존
`/api/item-transactions/commit`은 변경하지 않았고 아직 이 예약을 필수로 검사하지 않는다.

```text
POST /api/dungeon-sessions/{sessionId}/pickup-intents
GET  /api/dungeon-sessions/{sessionId}/pickup-intents/{requestId}
```

POST 본문은 `dropId`와 기존 `ItemCommitRequestContract` 형식의 `command`다.
게임 서버 토큰의 서버 ID·세션이 요청 경로와 정확히 일치해야 하며, 세션 없는 토큰,
플레이어·관리자·정산 후 제한 토큰으로는 사용할 수 없다. 현재 세션은 `InProgress`이고
만료되지 않아야 한다. 원래 캐릭터/계정의 멤버십·멤버 lease·같은 세션의 캐릭터 lease를
확인하고 불변 포기 영수증이 있으면 조회와 동일 예약 재시도도 거절한다.

예약 명령은 `Item.Pickup`, 단일 신규 Active 아이템, 원래 캐릭터 소유 Inventory의
0~79 슬롯, expected/new revision 0, `WorldPickup` 생성 출처와 잠금 해제 상태로 제한한다.
수량은 현재 BP int8 계약에 맞는 1~127이며 아이템 수량과 `affectedQuantity`가 같아야 한다.
이는 구조 검증이며 백엔드 콘텐츠 정의의 최대 스택이나 실제 처치/드롭 생성 증명은 아니다.

- `(sessionId, dropId)`마다 하나의 예약만 허용하고 `requestId`·`itemId`도 전역 유일하다.
- seed·목적지·수량·태그·스탯 등을 포함한 전체 정규화 명령을 고정한다. 선언된 fingerprint가
  같더라도 본문이 달라지면 충돌이며, 완전히 같은 재시도만 원래 기록/시각을 반환한다.
- Memory는 기존 던전 gate, PostgreSQL은 포기/입장/정산과 같은 세션 부모 행의
  `FOR UPDATE` 및 단일 트랜잭션을 사용한다. 호출자가 넘긴 컬렉션도 비동기 대기 전에 복사한다.
- `dungeon_pickup_intents`는 원래 서버·세션·드롭·요청·캐릭터·계정·아이템과 전체 명령을
  저장한다. 포기로 현재 멤버십이 제거돼도 기록은 남으며 `item_records`/영수증은 쓰지 않는다.
- POST는 `application/json`만 받고 본문을 128 KiB로 제한한다(chunked 포함).
  저장되는 정규화 명령은 UTF-8 64 KiB 이하이며 엔드포인트 응답은 `Cache-Control: no-store`다.

성공은 HTTP 200의 `Prepared`/`AlreadyPrepared`이며 원래 ID·시각과 `command`를 반환한다
(GET은 `Prepared`). 인증 실패 401, 권한 실패 403, 잘못된 본문/명령 400, 기록 없음 404,
예약 충돌 409, 본문 초과 413, 지원하지 않는 Content-Type 415다. 충돌/권한 실패 응답에
다른 예약의 본문을 노출하지 않는다. 404는 재발급/지급 허용을 뜻하지 않는다.

이 첫 단계에서는 예약 기록/조회만 추가했으며 원자적 소비·지급은 아래 후속 단계에서
구현했다. 취소·해제·목적지 변경, UE/BP 호출 연결, 미저장 요청 자동 replay와 월드
드롭/처치의 영속 레지스트리는 아직 없다. `dropId`는 현재 할당된 신뢰 게임 서버가 공급한다.

2026-10-13 실행, 2026-10-14 기록 마무리: 격리 Release 빌드 경고/오류 0,
Memory 18개 + 실제 PostgreSQL 21개(총 39개) 통과. 각 저장소의 실제 loopback Kestrel과
운영 인증 미들웨어/엔드포인트, 동시 예약·포기 경합·만료·정산/실패 상태, 새 DB 연결의
영속 조회·아이템/영수증 쓰기 0·강제 insert 롤백·실제 부모 행 lock wait를 확인했다.
전체 운영 호스트/UE/Steam/Shipping 검증은 아니며 테스트 전용 schema만 정리하고 기존
public 데이터는 유지했다. 이번 단계에서 실행 중 백엔드 재시작/public schema 이관은 하지 않았다.

증거: [Verification.proof.json](../Saved/Tests/PickupIntent-20261013-bfcc2a042829455aaa64a421d560fd9e/Verification.proof.json).
최초 로그 인코딩 판정 실패도 별도 보존하고 원본 로그와 소스/Debug·Release 어셈블리 사본을
구분해 기록했다. 재현은 로컬 개발 DB(`127.0.0.1:54329`, `project_rpg`) 연결 문자열을
`PROJECT_RPG_PICKUP_INTENT_TEST_POSTGRES` 환경 변수에 설정한 뒤 실행한다.

```powershell
dotnet run --project Backend/ProjectRpg.Backend.PickupIntentTests/ProjectRpg.Backend.PickupIntentTests.csproj -- --require-postgres
```

## 던전 픽업 예약의 원자적 소비·지급 (2026-10-14)

```text
POST /api/dungeon-sessions/{sessionId}/pickup-intents/{requestId}/commit
```

위 예약 API와 같은 정확한 서버/세션 토큰만 사용하며 **요청 본문은 비워야 한다**.
본문이 있으면 chunked 전송도 400으로 거절한다. 캐릭터·드롭·아이템·수량·목적지·명령은
이미 저장된 예약에서만 복원하므로 호출자가 지급 내용을 교체할 수 없다.

`DungeonPickupCommitService`는 기존 game/item singleton 저장소를 연결한다. Memory는
던전 gate 다음 기존 아이템/재화 transaction gate를 잡고 snapshot을 보관한다.
PostgreSQL은 세션 부모 `FOR UPDATE` 다음 기존 owner/request 잠금과 CAS·위치 유일성·
아이템/영수증 쓰기를 재사용한다. 아이템 신규 저장, 원래 영수증 저장과 예약의
`consumed_at` 설정은 함께 성공하거나 모두 롤백하며 원래 예약 본문/키/준비 시각은 바꾸지 않는다.
예약 조회/정확한 준비 재시도 응답에는 nullable `consumedAt`도 포함한다.

- 미소비 예약에서도 현재 `InProgress`·서버 할당·원래 계정/캐릭터의 멤버십과 lease·포기
  이력을 재검사한다. Memory는 두 gate 이후, PostgreSQL은 부모/owner/request 잠금 대기
  이후와 실제 쓰기 전에도 시계를 읽어 만료를 확인한다.
- 포기가 먼저면 지급하지 않는다. 지급이 먼저면 이미 커밋된 아이템은 회수하지 않지만,
  포기/만료/정산/종료 뒤 이 새 API의 조회성 커밋 재시도도 403으로 차단한다.
- 소비된 예약은 전체 본문과 일치하는 원래 영수증만 `AlreadyCommitted`로 반환한다.
  이후 이동/소비된 현재 아이템을 초기 위치/수량으로 덮어쓰지 않는다. 원래 영수증이
  사라졌으면 500으로 닫고 재지급하지 않으며, 본문이 달라졌으면 409다.
- 미소비 예약에 같은 Request ID의 일반/구형 영수증이 이미 있으면 본문이 같아도
  `IdempotencyConflict`다. 별도 경로의 성공을 원자적 예약 소비 증거로 소급 채택하지 않는다.
- `LocationConflict`/`RevisionConflict` 등 확정 거절에도 예약은 미소비로 남는다.
  새 Request ID·다른 수령인·다른 슬롯으로 재예약하거나 자동 해제하지 않는다.
  원래 슬롯이 비워지면 같은 명령을 다시 시도할 수 있지만 목적지 변경 프로토콜은 별도다.

200 성공 본문은 기존 Item API 영수증 형식의 `Committed`/`AlreadyCommitted`다.
인증 실패 401, 권한 실패 403, 없는 예약 404, 잘못된 입력/본문 400,
`IdempotencyConflict`/`RevisionConflict`/`LocationConflict` 409, 내부 실패 500이다.
실패에는 기존 예약/영수증 본문을 싣지 않고 엔드포인트 응답은 `no-store`다.
500·취소·통신 오류는 미저장 확정이나 다른 ID 지급 허용이 아니며 원래 요청을 유지한다.
DDL은 기존 테이블에도 nullable `consumed_at`를 추가하지만 이번 검증은 전용 schema에서만 했다.

격리된 최종 Release 빌드 경고/오류 0, 같은 격리 Release 어셈블리에서 Memory 37개+
실제 PostgreSQL 49개(총 86개) 통과했다. 기존 예약 39개에 새 커밋/직접 영향 회귀 47개를
더한 결과다. 두 저장소의 실제 loopback HTTP/운영 인증·본문 거절·원래 결과를 버린 뒤
재시도, 20개 동시 커밋, 포기/정산 경합·남은 파티 보존, 슬롯 충돌/동일 재시도,
새 DB 연결, 영수증/소비 표시 쓰기 강제 실패의 전체 롤백, 실제 부모/owner lock wait 중
만료·취소, 영수증 누락/본문 변조의 무재지급 및 기존 보상 transaction 호출을 확인했다.
최초 보상 회귀 fixture 형식 실패와 중간 Debug 84개 통과도 최종 Release 결과와 구분해 보존했다.
전용 schema만 정리하고 기존 public 데이터·실행 중 백엔드/UE/에디터는 유지했다.

증거: [Verification.proof.json](../Saved/Tests/PickupCommit-20261014-6abd53b3ba84480ebb342a06ab9d03fc/Verification.proof.json).
소스 및 실행 전후 격리 runtime/로그 해시를 함께 기록했다. 실행 방법은 위와 같다.
실제 HTTP 응답을 프록시로 유실한 검증, 전체 운영 호스트, UE/BP 통합, 자동 replay,
운영 Steam/다중 PC/Shipping 검증은 아니다. 기존 일반 Commit/영수증 API도 변경하지 않았다.
다음은 네이티브 안정 Drop ID와 prepare/commit 단계·기존 journal을 연결하되, 확정 거절을
예약 해제로 오해하지 않는 정책과 이전 journal/일반 Commit 호환 경계를 먼저 정하는 것이다.

## 검증

백엔드 실행 후 다음 스크립트로 메모리 저장소의 API 계약을 검사한다.

```powershell
Backend/item-smoke-test.ps1 `
    -BaseUrl http://127.0.0.1:3000 `
    -AdminToken $env:PROJECT_RPG_BACKEND_ADMIN_TOKEN
```

검사 범위는 생성, Revision CAS, 멱등 재시도, Request ID 충돌, 위치 충돌
롤백, 두 Record 원자적 변경, 소유자 조회, Terminal tombstone이다.
