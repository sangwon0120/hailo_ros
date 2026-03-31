import cv2
import time
import numpy as np
from hailo_platform import FormatType, HailoSchedulingAlgorithm, VDevice

import rclpy
from unitree_go2_vision_msgs.msg import Detection, DetectionArray

model_name = "hyolov8s"
hef_path = f"{model_name}.hef"

params = VDevice.create_params()
params.scheduling_algorithm = HailoSchedulingAlgorithm.ROUND_ROBIN
target = VDevice(params)

# =========================
# 기본 설정
# =========================
batch_size = 1
TIMEOUT_MS = 10000
CONF_THRES = 0.25

CLASS_NAMES = {
    0: "car",
    1: "person",
    2: "pole",
    3: "tree",
    4: "bollard",
}

# =========================
# 전처리 / 후처리 함수
# =========================
def letterbox(img, new_shape=(640, 640), color=(114, 114, 114)):
    shape = img.shape[:2]  # (h, w)

    r = min(new_shape[0] / shape[0], new_shape[1] / shape[1])
    new_unpad = (int(round(shape[1] * r)), int(round(shape[0] * r)))

    dw = new_shape[1] - new_unpad[0]
    dh = new_shape[0] - new_unpad[1]

    dw /= 2
    dh /= 2

    if shape[::-1] != new_unpad:
        img = cv2.resize(img, new_unpad, interpolation=cv2.INTER_LINEAR)

    top, bottom = int(round(dh - 0.1)), int(round(dh + 0.1))
    left, right = int(round(dw - 0.1)), int(round(dw + 0.1))

    img = cv2.copyMakeBorder(
        img,
        top,
        bottom,
        left,
        right,
        cv2.BORDER_CONSTANT,
        value=color,
    )

    return img, r, (dw, dh)


def parse_hailo_nms_output(nms_result, conf_thres=0.25):
    detections = []

    for class_id, class_boxes in enumerate(nms_result):
        if class_boxes is None:
            continue

        for box in class_boxes:
            box = np.array(box, dtype=np.float32).reshape(-1)

            if len(box) < 5:
                continue

            # Hailo NMS output format:
            # [y_min, x_min, y_max, x_max, score]
            y_min, x_min, y_max, x_max, score = box[:5]

            if score < conf_thres:
                continue

            detections.append({
                "class_id": class_id,
                "score": float(score),
                "bbox_xyxy": np.array([x_min, y_min, x_max, y_max], dtype=np.float32),
            })

    return detections


def scale_boxes_to_original(detections, orig_w, orig_h):
    for det in detections:
        x1, y1, x2, y2 = det["bbox_xyxy"]

        # normalized [0,1] -> original image pixel coordinates
        x1 *= orig_w
        x2 *= orig_w
        y1 *= orig_h
        y2 *= orig_h

        # clip
        x1 = max(0, min(x1, orig_w - 1))
        x2 = max(0, min(x2, orig_w - 1))
        y1 = max(0, min(y1, orig_h - 1))
        y2 = max(0, min(y2, orig_h - 1))

        det["bbox_xyxy"] = np.array([x1, y1, x2, y2], dtype=np.float32)

    return detections


def draw_detections(image, detections):
    vis_img = image.copy()

    for det in detections:
        x1, y1, x2, y2 = det["bbox_xyxy"].astype(int)
        score = det["score"]
        cls_id = det["class_id"]
        cls_name = CLASS_NAMES.get(cls_id, str(cls_id))

        if x2 <= x1 or y2 <= y1:
            continue

        cv2.rectangle(vis_img, (x1, y1), (x2, y2), (0, 255, 0), 2)
        cv2.putText(
            vis_img,
            f"{cls_name}: {score:.2f}",
            (x1, max(y1 - 5, 0)),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.5,
            (0, 255, 0),
            1,
        )

    return vis_img


# =========================
# Hailo 모델 준비
# =========================
infer_model = target.create_infer_model(hef_path)
infer_model.set_batch_size(batch_size)

print("Output names:", infer_model.output_names)

output_name = infer_model.output_names[0]
infer_model.output(output_name).set_format_type(FormatType.FLOAT32)

configured_infer_model = infer_model.configure()

input_h, input_w, input_c = infer_model.input().shape
print(f"Input shape: H={input_h}, W={input_w}, C={input_c}")


# =========================
# 카메라 열기
# =========================
cap = cv2.VideoCapture(0)

if not cap.isOpened():
    raise RuntimeError("카메라를 열 수 없습니다. /dev/video0 연결 상태를 확인하세요.")

cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)

prev_time = time.time()

print("실시간 추론 시작")
print("종료하려면 'q' 를 누르세요.")

rclpy.init()
node = rclpy.create_node('hailo_detection_publisher')
pub = node.create_publisher(DetectionArray, 'hailo_detections', 10)

while True:
    ret, frame = cap.read()
    if not ret:
        print("카메라 프레임 읽기 실패")
        break

    orig_h, orig_w = frame.shape[:2]

    # BGR -> RGB
    rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)

    # Letterbox preprocessing
    img_letterbox, ratio, (dw, dh) = letterbox(rgb, new_shape=(input_h, input_w))
    img_input = img_letterbox.astype(np.uint8)

    output_buffer = np.empty(infer_model.output(output_name).shape, dtype=np.float32)

    bindings = configured_infer_model.create_bindings(
        output_buffers={output_name: output_buffer}
    )
    bindings.input().set_buffer(img_input)

    result_holder = {"exception": None}

    def callback(completion_info):
        if completion_info.exception:
            result_holder["exception"] = completion_info.exception

    configured_infer_model.wait_for_async_ready(TIMEOUT_MS)
    job = configured_infer_model.run_async([bindings], callback)
    job.wait(TIMEOUT_MS)

    if result_holder["exception"] is not None:
        print(f"Inference error: {result_holder['exception']}")
        continue

    # 중요: output_buffer가 아니라 get_buffer()로 읽기
    nms_result = bindings.output(output_name).get_buffer()

    detections = parse_hailo_nms_output(nms_result, conf_thres=CONF_THRES)
    detections = scale_boxes_to_original(detections, orig_w, orig_h)

    ######## ROS 2 메시지 구성 및 발행
    msg = DetectionArray()
    msg.header.stamp = node.get_clock().now().to_msg()
    msg.header.frame_id = "camera_link"
    
    for det in detections:
        x1, y1, x2, y2 = det["bbox_xyxy"]
        score = det["score"]
        cls_id = det["class_id"]
        cls_name = CLASS_NAMES.get(cls_id, str(cls_id))
        
        d = Detection()
        d.class_id = int(cls_id)
        d.class_name = cls_name
        d.score = float(score)
        d.x_min = float(x1)
        d.y_min = float(y1)
        d.x_max = float(x2)
        d.y_max = float(y2)
        
        msg.detections.append(d)
        
    pub.publish(msg)
    
    # 노드 콜백 처리 (통신 유지)
    rclpy.spin_once(node, timeout_sec=0.0)

    vis_frame = draw_detections(frame, detections)

    current_time = time.time()
    fps = 1.0 / (current_time - prev_time) if current_time != prev_time else 0.0
    prev_time = current_time

    cv2.putText(
        vis_frame,
        f"FPS: {fps:.2f}",
        (10, 30),
        cv2.FONT_HERSHEY_SIMPLEX,
        1.0,
        (0, 0, 255),
        2,
    )

    cv2.imshow("Hailo Real-time Detection", vis_frame)

    key = cv2.waitKey(1) & 0xFF
    if key == ord("q"):
        break

cap.release()
cv2.destroyAllWindows()
node.destroy_node()
rclpy.shutdown()
print("종료 완료")