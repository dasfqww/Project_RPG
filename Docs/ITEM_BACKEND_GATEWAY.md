# Item Backend Gateway

## 목적

Dedicated Server가 Item V2 API를 비동기로 호출하는 경계다. 게임 규칙과
트랜잭션 모델은 HTTP를 모르며, 네트워크 계층도 아이템 규칙을 판단하지 않는다.

```text
Dedicated Server gameplay
  -> URPGItemBackendSubsystem
     -> FRPGItemBackendGateway
        -> FRPGItemBackendJsonCodec
        -> IRPGItemBackendTransport
           -> FRPGHttpItemBackendTransport
              -> Item V2 API
```

- `Codec`: Item V2 JSON과 `FRPGItemRecord` 변환
- `Transport`: HTTP 요청 실행만 담당
- `Gateway`: 입력 검증, 재시도, 응답 상관관계 검증
- `Subsystem`: Dedicated Server 수명과 자격 증명 소유

## 서버 설정

`DefaultGame.ini` 또는 서버 전용 설정에 다음 값을 지정한다.

```ini
[/Script/Project_RPG.RPGItemBackendSubsystem]
ApiUrl=http://127.0.0.1:3000/api
RequestTimeoutSeconds=10.0
MaximumAttempts=2
```

서비스 토큰은 설정 파일이나 클라이언트 빌드에 넣지 않는다. Dedicated Server
프로세스를 시작하기 전에 환경 변수로 주입한다.

```powershell
$env:PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN = '<session-scoped-game-server-token>'
```

토큰이 없거나 클라이언트 프로세스라면 Subsystem은 HTTP Gateway를 생성하지
않으며 `IsAvailable()`이 `false`를 반환한다.

## C++ 사용 계약

캐릭터 아이템 조회는 정규 Guid 문자열을 요구한다.

```cpp
URPGItemBackendSubsystem* Items =
    GameInstance->GetSubsystem<URPGItemBackendSubsystem>();

Items->LoadCharacterItems(
    CharacterId,
    [](FRPGItemBackendLoadResult Result)
    {
        if (Result.WasSuccessful())
        {
            // Result.Records를 서버의 읽기 모델에 반영한다.
        }
    });
```

변경 요청은 `FRPGItemRepositoryCommitRequest`를 완성한 뒤 `Commit`으로 보낸다.
호출자는 다음 값을 보장해야 한다.

- `RequestId`: 논리 명령마다 새 Guid
- `Operation`: 허용된 Item V2 작업 이름
- `CommandFingerprint`: 명령 내용에서 결정적으로 생성한 값
- `Actor`: 인증된 서버 측 행위자
- `Mutations`: 기대 Revision과 동일 Revision을 가진 새 Record

완료 콜백의 `Status`가 `Succeeded` 또는 `AlreadyApplied`일 때만 성공으로
처리한다. UI와 복제 상태는 성공 응답의 Record를 기준으로 갱신한다.

## 재시도와 멱등성

전송 실패, HTTP 408/425/429, 5xx는 `MaximumAttempts` 범위에서 자동으로
재시도한다. Commit은 최초 직렬화 결과를 보관하고 재시도마다 다음 값을 그대로
재사용한다.

- Request ID
- Command fingerprint
- JSON body

따라서 응답만 유실된 경우 백엔드 receipt가 동일 결과를 반환하며 명령이 두 번
적용되지 않는다. 같은 Request ID를 다른 명령에 재사용하면
`IdempotencyConflict`로 처리해야 한다.

응답의 Request ID, Operation, Command fingerprint, Actor가 요청과 다르면
Gateway는 성공 응답이어도 `ProtocolError`로 거부한다.

## 숫자 정밀도

Revision은 JSON의 숫자 토큰으로 전송하지만 `double`을 거치지 않는다. Codec은
숫자를 문자열 표현으로 보존한 뒤 `int64`로 변환하므로 `2^53`보다 큰 Revision도
정확하게 왕복한다.

## 현재 범위와 다음 단계

현재 Gateway는 캐릭터 전체 Record 조회와 원자적 Commit 경계를 제공한다.
`FFastArraySerializer` 기반 Inventory Projection도 구현되어, 인증 직후 조회한
Record를 소유자 전용 읽기 모델로 변환해 델타 복제한다. Legacy Definition
Adapter와 장착·해제·소모 도메인 명령도 구현됐다. 성공한 Commit receipt는
owner와 상태를 검증한 뒤 부분 Record를 전체 Projection 캐시에 병합하며, 낮은
Revision의 늦은 응답은 거부한다. 다음 단계는 Dedicated Server 비동기 command
orchestrator와 Equipment/GAS reconciler, Projection 기반 UI 전환이다. 던전 서버
인스턴스와 세션 범위 권한은 별도 요청 컨텍스트와 백엔드 정책으로 추가한다.

Projection 계약은
[`Docs/ITEM_INVENTORY_PROJECTION.md`](ITEM_INVENTORY_PROJECTION.md)를 참고한다.

장착·해제·소모와 Commit 이후 효과 적용 계약은
[`Docs/ITEM_ACTION_TRANSACTIONS.md`](ITEM_ACTION_TRANSACTIONS.md)를 참고한다.

Item V2 HTTP 계약은
[`Backend/ITEM_API_V2.md`](../Backend/ITEM_API_V2.md)를 참고한다.
