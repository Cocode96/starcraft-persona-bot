# StarCraft Persona Bot

스타크래프트 리플레이에서 개인별 선택을 추출하고, 직접 상대할 수 있는 BWAPI 봇에 반영한다. 사용자가 확정한 목표는 Lavaa와 지인 각각의 평소 플레이를 재현하는 대전 봇이다. Pluto급 실력은 요구사항이 아니다. 구현과 평가는 개인별 선택과 행동의 유사성을 기준으로 한다.

현재 버전은 저그, 테란, 프로토스의 생산 명령 비율과 실제 경기의 빌드 순서를 사용하는 통계적 모방 모델이다. 신경망이나 자가 대전 리그 학습은 사용하지 않는다. 사람별 플레이의 재현 정도와 실제 대전 동작은 아직 검증하지 않았다.

## 개인별로 반영한 것

- 상대 종족과 경기 시간 구간별 유닛 생산 명령 비율. 각 경기에 같은 가중치를 주고, 상대 종족 표본이 3경기 미만이면 해당 사람의 전체 경기로 보완한다.
- 학습에 사용한 한 경기의 건설, 연구와 첫 업그레이드 명령 순서. 여러 경기의 건설 시점 중앙값을 섞지 않는다. 최대 96개 명령을 사용한다.
- 초반 일꾼 생산 명령 수로 추정한 일꾼 목표와 본진에서 먼 곳을 대상으로 한 첫 공격 명령 시점.

채취, 보급 관리, 건설 위치 선정, 정찰, 방어와 유닛 컨트롤은 공통 규칙이다. 봇은 실제 자원과 생산 가능 여부를 확인하므로 리플레이 입력 시각과 실행 시각은 달라질 수 있다. 경기마다 새 빌드를 고르려면 내보내기의 `--seed`를 바꾼다. 동일한 패키지는 같은 빌드와 난수 시드로 시작한다.

## 로컬 검증 결과

2026-10-12 기준 원본 431개를 읽고 바이트 중복 6개와 저장 형태가 다른 동일 경기 1개를 제외해 424경기를 분석했다. 파싱 오류는 없었다. 원본은 수정하지 않았다.

사람 두 명이 서로 다른 팀으로 참여한 1,440프레임 이상 경기를 종족별로 분류했다. 각 사람의 가장 많이 플레이한 종족을 선택하고, 최근 약 20%는 평가용으로 남겼다. 배포 모델에도 평가 경기의 입력은 포함하지 않는다.

| 리플레이 닉네임 | 종족 | 학습 경기 | 평가 경기 |
| --- | --- | ---: | ---: |
| Lavaa | Zerg | 141 | 35 |
| Doran_e | Protoss | 91 | 22 |
| dnfla9407 | Protoss | 84 | 20 |
| Flash_Bangg | Terran | 61 | 15 |
| sdh321654 | Terran | 37 | 9 |
| 9666 | Protoss | 16 | 3 |
| kkami77777 | Zerg | 36 | 9 |

평가는 지원하는 유닛의 생산 명령 예측 정확도와 음의 로그 가능도를 측정한다. 비교 대상은 같은 종족의 여러 사람을 합친 모델이며, 평가 경기와 평가 시작 시점 이후의 경기를 제외한다. 개인 모델이 항상 비교 모델보다 낫지는 않았다. 이 지표는 승률이나 사람과 닮은 정도를 뜻하지 않는다. 상세 수치와 분할 경기 ID는 로컬 `data/roster/*/model.json`의 `evaluation`에 있다.

Python 테스트 15개, Windows x86 DLL 빌드, C++ 설정 테스트와 DLL 로드 테스트 2개를 통과했다. 사람 7명과 상대 종족별 설정을 합한 28개 INI에 대해 실제 BWAPI 타입 ID, 종족과 설정 계약을 검사했다. 이는 게임을 실행한 검증이 아니다.

현재 설치된 게임은 리마스터 `1.23.10.13515`이며, 별도의 브루드워 1.16.1은 없다. 게임 실행 검증은 해당 환경 확보 전까지 미완료다. GitHub 원격과 클라우드 실행도 아직 없다.

## 준비와 학습

Python 3.11 이상, Visual Studio 2022의 C++ 데스크톱 개발과 CMake 도구가 필요하다. 저장소 루트에서 실행한다.

```powershell
python scripts/bootstrap.py --bwapi
python -m persona.cli scan "$env:USERPROFILE/Documents/StarCraft" --output data/replays-v2.json
python -m persona.cli players --input data/replays-v2.json --anchor Lavaa --limit 20
python -m persona.cli train-roster --seed 42
python -m unittest discover -s tests -v
python scripts/build_bot.py --roster data/roster
python scripts/select_persona.py
python scripts/select_persona.py Lavaa
```

`bootstrap.py`는 공식 릴리스의 고정 버전 screp v1.13.7과 BWAPI 4.4.0을 `tools/`에 준비하고 SHA-256을 대조한다. 분석과 통계 모델에는 외부 Python 패키지, GPU와 OpenAI API 키가 필요하지 않다. 설치된 게임을 수정하지 않는다.

`train-roster`는 로컬 `data/player-identities.json`에서 사용자가 확인한 닉네임과 이름을 읽는다. 이 파일과 모든 개인 모델, 원본 리플레이는 Git에서 제외한다. 처음 복제한 저장소에서 여러 사람을 학습하려면 다음 형식의 파일을 준비한다. Lavaa는 기본 포함된다.

```json
{"players": [{"nickname": "ExactNickname", "display_name": "ConfirmedName"}]}
```

한 명만 처리할 수도 있다.

```powershell
python -m persona.cli train Lavaa --race Zerg --output data/Lavaa-model.json
python -m persona.cli export-model data/Lavaa-model.json --opponent Protoss --seed 42 --output data/Lavaa-vs-Protoss.ini
```

`scan`은 파일 SHA-256과 시작 시각, 맵, 참가자 정보로 중복을 묶는다. 같은 닉네임이라도 다른 문자열을 자동으로 같은 사람으로 합치지 않는다. 기존 `profile`과 `export-bot` 명령은 초기 저그 중앙값 프로필 호환용으로 남아 있다.

## 사람 선택과 실행 패키지

`dist/players/<닉네임>-<종족>/bwapi-data/`에 각 사람의 DLL과 설정 4개가 있다. DLL은 공통이고 개인별 모델 설정이 다르다. `select_persona.py`에 닉네임 또는 확인된 이름을 입력하면 `dist/bwapi-data/`에 선택한 패키지를 복사한다. 게임 설치나 실행은 하지 않는다.

모델은 기본 `persona.ini`와 상대 종족별 `persona-Zerg.ini`, `persona-Terran.ini`, `persona-Protoss.ini`로 구성된다. 게임 시작 시 BWAPI가 알려주는 상대 종족이 확정되어 있으면 해당 설정을 사용한다. 랜덤 또는 알 수 없는 상대는 기본 설정을 쓴다. 숨겨진 적 유닛 정보는 조회하지 않는다.

합법적으로 확보한 브루드워 1.16.1과 BWAPI 4.4.0 실행 환경이 준비되면 다음 순서로 검증한다.

1. 원하는 사람을 `select_persona.py`로 선택한다.
2. `dist/bwapi-data/`의 내용을 별도 1.16.1 게임 폴더의 `bwapi-data/`에 복사한다.
3. 해당 설치의 `bwapi-data/bwapi.ini`에서 `[ai]`의 `ai`를 `bwapi-data/AI/PersonaBot.dll`로 설정한다.
4. BWAPI가 활성화된 Chaoslauncher에서 실행하고 선택한 사람의 종족으로 설정한다.
5. 사람과 대전하려면 BWAPI가 활성화된 봇 인스턴스와 주입을 끈 사람 인스턴스로 Local PC 방에 입장한다.

화면에 닉네임과 학습 표본 수가 표시된다. 종족이나 모델 타입이 맞지 않으면 봇이 중지하고 이유를 출력한다. 설정이 없거나 파싱에 실패하면 명시적인 `Baseline Zerg` 규칙으로 돌아간다. 결과는 `bwapi-data/write/persona-results.txt`에 남긴다. 리마스터 설치에 DLL만 복사해서 실행할 수는 없다.

## 알려진 한계

리플레이는 성공한 행동이 아닌 입력 명령을 제공한다. 취소, 실패, 같은 위치의 재건설과 반복 업그레이드 레벨을 복원하지 않는다. 건물 명령 시각을 완성 시각으로 해석하면 안 된다. 공격 시점과 일꾼 목표는 추정값이다. UMS 관전 맵이 필터를 통과할 수 있고 맵 규칙은 별도 검토가 필요하다. 분석 결과에는 게임 채팅을 저장하지 않는다.

지원 유닛만 생산하고, 아칸 합체, 수송과 드롭, 섬맵 이동, 일부 마법과 상황별 빌드 전환은 구현하지 않았다. 확장 위치와 일꾼 배분은 단순 규칙이며 탐색되지 않은 자원 지역에는 확장하지 못할 수 있다. 교전, 장기 운영과 자원 정체 여부는 실제 게임에서 확인해야 한다. 공통 실행 규칙이 개인 특성을 얼마나 바꾸는지도 아직 측정하지 않았다.

## 코드와 클라우드 환경

| 경로 | 역할 |
| --- | --- |
| `persona/` | 명령 추출, 통계 모델 학습, 분할 평가와 INI 내보내기 |
| `bot/` | BWAPI C++ 모듈과 모델 설정 해석 |
| `scripts/` | 도구 준비, DLL 빌드와 사람 선택 |
| `tests/` | 데이터 분할, 확률 선택, 설정 계약과 DLL 로드 검사 |
| `data/`, `dist/` | 로컬 개인 데이터와 실행 산출물, Git 제외 |

Visual Studio 솔루션은 `build/StarCraftPersonaBot.sln`이다. `persona/unit_types.json`은 BWAPI 4.4.0 정적 타입 정보이며 빌드된 `unit_catalog.exe`로 다시 생성할 수 있다.

현재 단계는 로컬 Windows 환경이 적합하다. Codex Cloud는 GitHub에 저장소를 올린 후 리플레이 분석 코드와 테스트 작업에 사용할 수 있다. 클라우드 환경의 저장소로 이 프로젝트를 선택하고 Python 3.11 이상, CMake와 C++17 컴파일러를 준비한 뒤 설정 명령으로 다음을 사용한다.

```bash
bash scripts/cloud_setup.sh
```

이 스크립트는 Python과 BWAPI에 의존하지 않는 C++ 설정 테스트를 실행한다. 게임 DLL 빌드와 대전은 Windows 환경이 필요하다. 개인 데이터는 Git에 없으므로 클라우드에서 재학습하려면 별도로 전달해야 한다. GitHub Actions 설정은 포함되어 있지만 원격 실행은 검증하지 않았다.

## 참고

- [BWAPI 공식 저장소](https://github.com/bwapi/bwapi)
- [screp 리플레이 파서](https://github.com/icza/screp)
- [Codex Cloud 환경 안내](https://learn.chatgpt.com/docs/environments/cloud-environments)

외부 도구와 SDK는 원본 프로젝트의 라이선스를 따른다. 게임 실행 파일과 데이터는 포함하지 않는다.
