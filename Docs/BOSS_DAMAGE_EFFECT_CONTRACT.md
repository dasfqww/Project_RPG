# 보스 피해 효과 계약과 회귀 복구

2026-10-08~09: 인증 시계 작업에서 발견했던 `ProjectRPG.Boss.Damage`와
`ProjectRPG.Boss.RaidContent` 실패를 최신 공유 수정과 대조하고 다시 검증했다.
이 턴을 시작할 때 런타임 호환 수정과 비정상 피해 테스트의 기대값 수정은 이미
반영돼 있었다. 이를 중복 수정하거나 이번 턴의 새 런타임 구현으로 집계하지 않는다.

## 원인과 현재 계약

저장된 `GE_Shared_ApplyDamage`는 단일 네이티브 피해 execution을 갖는 Instant GE이며,
대상 태그 조건을 검사하는 네이티브 `UTargetTagRequirementsGameplayEffectComponent`를
포함한다. 새 공통 피해 guard가 이 컴포넌트를 허용하지 않아 실제 Strike/HeavyStrike도
거부했다. 공유 수정은 정확한 네이티브 클래스만 허용하며 GAS의 태그 조건은 유지한다.
엔진의 해당 컴포넌트는 적용 조건/제거 조건을 검사하며 임의 피해 execution을 추가하는
기능이 아니다. Instant 정책, 직접 modifier 없음, 단일 정확한 TakeDamage execution,
추가 효과/사용자 정의 컴포넌트 거절 및 서버 최종 피해 상한은 그대로다.

`ApplyGameplayEffectSpecHandleToServerHit`의 **피해 spec** 성공은 GAS 적용 성공과
서버 피해 receipt 승인 모두를 요구한다. 보스의 incoming/targeted damage 경로도 이
계약을 따른다. 잘못된 공격/방어 능력치, 최종 피해 상한 초과 또는 정수 절삭 후 0 피해는
false다. 기존 테스트처럼 GE 적용만 성공하고 피해는 0이라고 기대하면 안 된다.
승인과 순 HP 감소량/위협도는 서로 다른 값이다. 치명타격, 동기 상태 전환·콜백 등으로
성공 후 위협도가 추가되지 않을 수 있으므로 실제 HP 차이와 보스 수명주기를 확인한다.

피해/타겟/페이즈/사망/위협도는 서버 소유다. 클라이언트 적중 승인 RPC나 예외를
추가하지 않았다. 구체 Ability의 commit, 서버 hit query, 전투 세대, 중복 타격 및 종료
정리도 계속 필요하다. 태그 조건 삭제, Blueprint/CDO 변경, 피해 보안 상한 완화는 없다.

## 이번 턴의 보강

- `RPGBossDamageTests.cpp`: 실제 shared GE를 운영 Threat/서버 guard로 적용한다.
  무적 대상 거절 시 HP/위협도/출력이 불변이고, 정상 대상에는 승인된 HP 감소 1과
  위협도 1이 발생함을 검사한다. 기존 페이즈 전환 회귀를 오염시키지 않도록 1 피해만
  사용하고 테스트 HP/위협도를 복구한다. 에셋/CDO는 수정하지 않는다.
- `RPGSkillDamageSecurityTests.cpp`: 허용된 네이티브 태그 조건과 금지된 추가 효과
  컴포넌트를 함께 넣어도 Supports/서버 적용이 거절되고 HP가 유지되는 사례를 추가한다.
  허용된 컴포넌트 하나가 나머지 컴포넌트 검사를 생략시키지 않는지 확인한다.
- `Run-BossStateAutomationTest.ps1`: 필수 12개 성공 마커에 더해 발견된 전체 보스
  결과가 Success인지 검사한다. 테스트 전후 DLL SHA256 일치를 요구하고 개별 결과와
  범위를 `Boss.proof.json`으로 남긴다. 종료 코드 0만으로 통과하지 않는다.
  현재 성공 로그·이전 실패 로그·빈 결과·미래 테스트의 Skipped 결과를 사용한 판정
  자체 검사 4개도 통과했다. 알려진 12개만 성공했다고 다른 실패를 무시하지 않는다.
- `Run-BossStateNetworkTest.ps1`: IrisRpc/NetPackageMap 등 전체 Iris*/Net* Error와
  probe 실패를 거절하고, 완료/이탈 확인 후 2초 동안 프로세스·로그를 추가 검사한다.
  Warning/Display를 Error로 오인하지 않는 구문/정규식 자체 검사 8개도 통과했다.

## 검증 기록

- Editor 증분 빌드: `Saved/Logs/BossDamageContractVerificationBuild-20261008.log` 성공.
  변경한 테스트 C++ 두 개를 컴파일하고 링크했다. 클린 빌드/Shipping 또는 프로젝트
  전체 경고 0건의 증거는 아니다.
- 보스 자동화 12개 성공:
  `Saved/Tests/BossState/Automation-d31ae6fb90944937a1ece870c47c6371/automation.log`,
  `Boss.proof.json`. 실제 저장된 BT/Strike/HeavyStrike의 격리 실행도 포함한다.
- 보안 자동화 38개 성공:
  `Saved/Tests/Security/Automation-51df77aad89a4d1cadb1e507eeec5204/automation.log`,
  `Security.proof.json`.
- 던전/결과/로비/인증 13개 성공:
  `Saved/Tests/DungeonFlow/Automation-1f850895bad74b8283a78a144f23fdcd/automation.log`.
- 보상 스냅샷 3개 성공:
  `Saved/Tests/RewardSnapshot/Automation-2f10e3845feb42f082ed2969aa5e9d28/automation.log`.
  총 66개이며 실패/Fatal/Assertion/Ensure 0건이다. 보스·보안 proof는 동일한 DLL 해시
  `F75B263DB28249A3D41AE26E95E476C61F751B272668E136E7104D2E4751C48F`를 확인했다.

### 네이티브 멀티플레이 회귀

- 전용 서버 + 원격 클라이언트 2개:
  `Saved/Tests/BossState/8b4cf9c5893841e6a3bce35e09c3cca9/` 통과.
- 리슨 서버 + 원격 클라이언트 2개:
  `Saved/Tests/BossState/c7f2811877b0453a98e2c871cddfdaec/` 통과.

두 실행 모두 강화된 오류 판정과 완료 후 2초 추가 검사를 사용했다. 서버 승인 피해와
위협도, HP 69→38, 공격 예고와 로컬 장판 상태, 늦은 참가, 리셋/전투 2 재시작/사망,
이전 전투 피해 거절을 확인했다. 첫 클라이언트의 실제 종료 뒤 서버의 타깃 대체,
BT/GAS 취소와 threat 정리도 확인했다. 이는 네이티브 격리 맵의 회귀다.

### 실제 저장된 보스 맵 회귀

- 전용 서버 + 원격 클라이언트 2개:
  `Saved/Tests/BossMap/04c7ec04868046598d76709ca0ef1dbe/` 통과.
  실제 BT 추적 이동 약 574cm, 자연 Strike의 플레이어 HP 300→269를 확인했다.
- 리슨 서버 + 원격 클라이언트 2개:
  `Saved/Tests/BossMap/9c409057854441f098fdfa15f9a7382a/` 통과.
  실제 BT 추적 이동 약 583cm, 자연 Strike의 플레이어 HP 300→238을 확인했다.

실제 bossmap/GameMode/BP/BT/공유 GE 경로를 사용했고, 준비 장벽 이후 자연 perception,
navigation/BT 이동과 GAS 공격을 확인했다. 수동 피해 주입·직접 공격 Ability 활성화는
없다(`InjectedDamage=0 ManualAbility=0`). 두 원격 클라이언트 모두 보스 이동, 공격 예고,
장판 상태와 실제 피해 복제를 확인했다. 피해량을 고정하지 않으며 기존 속성/치명타
계산을 사용한다. 두 실행의 완료 후 2초 오류 검사와 자체 프로세스 정리도 통과했다.

```powershell
& .\Scripts\Network\Run-BossStateNetworkTest.ps1 -TimeoutSeconds 240 -SkipSDKSetup
& .\Scripts\Network\Run-BossStateNetworkTest.ps1 -ListenServer -Port 18792 -TimeoutSeconds 240 -SkipSDKSetup
& .\Scripts\Network\Run-BossMapE2ETest.ps1 -NaturalCombat -TimeoutSeconds 600 -SkipSDKSetup
& .\Scripts\Network\Run-BossMapE2ETest.ps1 -NaturalCombat -ListenServer -Port 18813 -TimeoutSeconds 600 -SkipSDKSetup
```

네 실행의 서버/두 클라이언트 로그를 종료 후 다시 검사해 모든 프로세스의 Iris 활성화와
probe 실패/Fatal/Assertion/Ensure/Iris*/Net* Error 0건을 확인했다. 콘텐츠/초기화 경고 등
프로젝트 전체 무오류를 뜻하지 않는다. 검증 후 DLL 해시는 위 proof와 같고 생성한
테스트 프로세스와 UDP 18791/18792/18812/18813 리스너는 남지 않았다.

이전 실패 로그 `Automation-34c5ea64cae74969a2e0ea45ced77b87`와 인증 검증 당시 DLL의
기록은 보존한다. 이전 실패를 지우거나 이전 인증 E2E를 새 DLL의 전체 보스/전투
통과 증거로 대체하지 않는다. 최초 실제 맵 실행
`Saved/Tests/BossMap/be44461bd5ad4a9bab16d3bade220ee9/`에는 자연 피해와 양쪽 완료
마커가 남았지만 작업 중단으로 하네스의 최종 추가 검사/종료를 확인하지 못했다.
이 실행은 전체 통과로 집계하지 않고 위 전용 서버 재실행으로 대체했다.

실제 Steam, backend 장비 획득/장착부터의 자연 전투·정산 연속 흐름, 모든 스킬/AOE,
GPU 연출, 패키징, 지연/손실 및 돌발 Pawn 파괴의 이동 RPC 경합은 이번 회귀의
범위 밖이다. 실제 맵 하네스는 `-nosteam`/명시적 네트워크 테스트 입장 우회를 사용하므로
백엔드 Join Ticket 인증 검증이 아니다. 화면은 NullRHI이며 배포용 Server 타깃이 아니라
Development Editor 프로세스다. 이번 턴은 PostgreSQL/보상 HTTP E2E를 재실행하지 않았다.
관련 아키텍처/기존 플레이어 입력·맵 증거는
[RAID_MONSTER_AI_ARCHITECTURE.md](RAID_MONSTER_AI_ARCHITECTURE.md)를 따른다.
