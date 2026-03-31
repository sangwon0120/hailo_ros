# Hailo Robot Control Workspace

라즈베리 파이 5(RPi 5)와 Hailo NPU를 활용하여 Unitree 로봇의 비전 추론 및 제어를 수행하기 위해 구축된 독립 ROS 2 워크스페이스입니다.

## 1. 개요 (마이그레이션 과정)
본 워크스페이스는 초기 개발 단계에서 `unitree_ros2/example/src/src/sangw/` 위치에 임시 보관되던 AI 추론 및 제어 코드들을 완전히 독립적인 모듈로 사용할 수 있도록 이전(Migration)하여 생성되었습니다.

- **분리 이유**: 기존의 거대한 `unitree_ros2` 예제 코드베이스에서 벗어나 RPi 5 타겟 보드용으로 작고 빠르게 컴파일할 수 있게 만들기 위함입니다.
- **오버레이 기술(Overlay Workspace)**: 이 패키지는 `cyclonedds_ws`나 커스텀 메시지 패키지(`unitree_go2_vision_msgs`) 등 하위 구조체들을 내부적으로 복사하지 않습니다. 대신 기본 통신 라이브러리(`unitree_ros2`)를 **사전에 링킹(Underlay)** 하는 방식으로 미들웨어 충돌 없이 완벽히 연동됩니다.

## 2. 패키지 구성
- **hailo_robot_control**: 기존 `sangw` 밑에 있던 C++(`hailo_subscriber.cpp` 등) 노드, 제어용 유틸리티(`ros2_sport_client`), 그리고 비전 인식을 퍼블리시하는 Python(`hailo_detection.py`) 스크립트 모음

## 3. 사전 요구사항 (Prerequisites)
이 워크스페이스를 RPi 5에서 컴파일하고 실행하려면 **반드시 로봇의 기반 인터페이스인 `unitree_ros2`가 먼저 구성**되어 있어야 합니다.

1. `~/unitree_ros2` 폴더를 RPi 5에 전체 복사합니다.
2. `~/unitree_ros2/cyclonedds_ws` (또는 `unitree_go2_vision_msgs`) 등의 필수 통신 패키지를 먼저 빌드합니다.

## 4. 빌드 방법
의존성 꼬임을 방지하기 위해 반드시 **Underlay(unitree_ros2) 설정값을 먼저 로드**한 뒤 빌드해야 합니다.

```bash
# 1. 뼈대(통신 구조) 로드
source ~/unitree_ros2/install/setup.bash

# 2. 본 워크스페이스로 이동
cd ~/hailo_robot_ws

# 3. 빌드 시작
colcon build
```

## 5. 사용 방법
빌드가 성공적으로 끝났다면, 생성된 `setup.bash`를 터미널에 로드한 후 각 노드들을 바로 실행할 수 있습니다.

### 터미널 환경 세팅 (공통)
새 터미널을 열 때마다 실행할 명령을 지정해 주어야 합니다:
```bash
# 기본 통신 환경변수 (경우에 따라 생략 가능하나 RPi 재부팅 시 필수)
source ~/unitree_ros2/install/setup.bash

# Hailo 제어 패키지 환경변수
source ~/hailo_robot_ws/install/setup.bash
```

### 파이썬 노드 (Hailo NPU 기반 객체 추론 발행기) 실행
카메라를 켜서 객체를 추론하고 바운딩 박스를 `/hailo_detections` 토픽으로 쏘아줍니다.
```bash
ros2 run hailo_robot_control hailo_detection.py
```

### C++ 노드 (Detection 결과 수신 및 제어 구독기) 실행
파이썬 NPU 노드에서 보낸 바운딩 박스를 받아 실시간으로 터미널에 콘솔 출력합니다. 향후 제어 로직을 붙이기 좋은 뼈대입니다.
```bash
ros2 run hailo_robot_control hailo_subscriber
```
