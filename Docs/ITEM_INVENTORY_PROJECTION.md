# Item Inventory Projection

## 목적

권위 있는 `FRPGItemRecord` 전체를 클라이언트에 노출하지 않고, 현재 캐릭터의
인벤토리 UI에 필요한 읽기 모델만 소유자에게 델타 복제한다.

```text
Item V2 API
  -> URPGItemBackendSubsystem
     -> FRPGItemRecord[]
        -> FRPGInventoryProjectionMapper
           -> URPGInventoryProjectionComponent
              -> FRPGInventoryProjectionList (FFastArraySerializer)
                 -> owning client UI
```

`URPGInventoryProjectionComponent`는 `ARPGPlayerController`의 기본 서브오브젝트다.
Dedicated Server의 참가 티켓 검증과 캐릭터 ID 설정이 끝난 뒤 초기 조회를 시작한다.
기존 `URPGInventoryComponent`와 `URPGItemBase` 경로는 자산/UI 마이그레이션이 끝날
때까지 그대로 유지한다.

## 복제 계약

- `Projection`과 `LoadState`는 `COND_OwnerOnly`로 복제한다.
- 배열 항목은 `ItemId`를 안정 식별자로 사용한다.
- 동일 스냅샷은 델타를 만들지 않는다.
- 추가·수정 항목은 `MarkItemDirty`, 제거는 `MarkArrayDirty`로 전송한다.
- 클라이언트는 `GetProjectedItems`, `FindProjectedItem`으로만 읽는다.
- `OnProjectionChanged`, `OnLoadStateChanged`로 UI 갱신 시점을 알린다.

복제하는 필드는 다음과 같다.

- Item ID, Definition ID/Version
- 슬롯, 수량, `int64` Revision
- 귀속, 내구도, 만료 시각, 잠금 여부
- 인스턴스 태그와 롤된 스탯

Owner ID, Container ID, 생성 시드, 생명주기, 생성 출처 같은 서버 영속성 정보는
복제하지 않는다.

## 보안과 무결성

Mapper는 스냅샷을 적용하기 전에 전체 응답을 검증한다.

1. 예상 소유자는 유효한 Character여야 한다.
2. 모든 Record는 구조적으로 유효하고 예상 소유자와 일치해야 한다.
3. 다른 소유자의 Record가 하나라도 섞이면 전체 스냅샷을 거부한다.
4. Active + Inventory Record만 투영하고 Trade/Storage/Equipment/Terminal 등은 숨긴다.
5. 중복 Item ID 또는 중복 슬롯이 있으면 전체 스냅샷을 거부한다.
6. 슬롯과 Item ID 순으로 정렬해 결과를 결정적으로 유지한다.

비동기 조회 콜백은 로드 세대와 캐릭터 GUID를 다시 확인한다. 재접속이나 캐릭터
전환 뒤 도착한 오래된 응답은 적용하지 않는다. GUID는 소문자 하이픈 형식으로
정규화해 표기 차이로 인한 잘못된 소유자 불일치를 막는다.

## 상태 변경 규칙

이 Projection은 읽기 모델이다. 클라이언트 RPC나 UI 입력으로 Record를 직접
변경하지 않는다. 아이템 이동·스택·장착·소비는 서버 트랜잭션과 Item V2 Commit을
통과해야 하며, 성공 응답의 Record로 Projection을 다시 조정해야 한다.

인증 직후의 초기 Load는 `ApplyAuthoritativeRecords`로 전체 캐시를 교체한다.
이후 성공한 Item V2 Commit은 `ApplyAuthoritativeCommitResult`로 검증한 뒤 변경된
Record만 `FRPGInventoryProjectionStore`에 병합한다. 전체 캐시에서 read model을
다시 만들기 때문에 Commit에 포함되지 않은 Inventory 아이템은 유지된다. 더 낮은
Revision의 늦은 응답은 거부한다. 다음 구현에서는 비동기 명령 orchestrator가 이
API를 호출하고 Equipment/GAS read model도 같은 방식으로 조정한다.

## 검증

- UnrealHeaderTool 성공
- Projection 구현, PlayerController 생성, GameMode 인증 연결 컴파일 성공
- `UnrealEditor-Project_RPG.dll` 링크 성공
- `ProjectRPG.Item` 자동화 테스트 15개 성공
  - 다른 소유자 응답 전체 거부와 Inventory 전용 필터 검증
  - `2^53`보다 큰 Revision의 정확한 유지 검증
  - 안정 Item ID 기반 추가·무변경·수정·제거 델타 검증
  - 부분 Commit 병합 시 미변경 아이템 유지, 장착·소모 제거, 낮은 Revision 거부 검증
