#!/usr/bin/env bash
set -euo pipefail

NS="free5gc"

UE_DEPLOY="ueransim-ue"
UE_CONTAINER="ue"

GNB_DEPLOY="ueransim-gnb"

AMF_DEPLOY="free5gc-core-free5gc-amf-amf"
AMF_CONTAINER="amf"
AMF_LABEL="app.kubernetes.io/name=free5gc-amf"

UE_IMAGE_REPO="tcedzyns/ueransim-llm-poc"
UE_IMAGE_TAG="ciot-ulnas-size-$(date +%Y%m%d-%H%M%S)"
UE_IMAGE="${UE_IMAGE_REPO}:${UE_IMAGE_TAG}"

LOG_FILTER="SCTP Accept|NGSetup|InitialUE|Registration Accept|Registration Complete|Handle UL NAS Transport|Transport 5GSM|CIOT|CIoT|bytes|sha256|preview|too large|NGAP decode|error|fail"


make -j"$(nproc)"

echo "Image: ${UE_IMAGE}"

docker build -t "${UE_IMAGE}" .
docker push "${UE_IMAGE}"


kubectl -n "${NS}" set image deploy/"${UE_DEPLOY}" "${UE_CONTAINER}"="${UE_IMAGE}"

echo "--- Restarting AMF safely because it uses static N2/Multus IP ---"
kubectl -n "${NS}" scale deploy/"${AMF_DEPLOY}" --replicas=0

echo "--- Waiting for old AMF pod to disappear ---"
while kubectl -n "${NS}" get pods -l "${AMF_LABEL}" --no-headers 2>/dev/null | grep -q .; do
  kubectl -n "${NS}" get pods -l "${AMF_LABEL}" -o wide
  sleep 2
done

kubectl -n "${NS}" scale deploy/"${AMF_DEPLOY}" --replicas=1
kubectl -n "${NS}" rollout status deploy/"${AMF_DEPLOY}" --timeout=180s

echo "--- Restarting gNB ---"
kubectl -n "${NS}" rollout restart deploy/"${GNB_DEPLOY}"
kubectl -n "${NS}" rollout status deploy/"${GNB_DEPLOY}" --timeout=180s

echo "--- Waiting a few seconds for gNB to reconnect to AMF ---"
sleep 5

echo "--- Restarting UE ---"
kubectl -n "${NS}" rollout restart deploy/"${UE_DEPLOY}"
kubectl -n "${NS}" rollout status deploy/"${UE_DEPLOY}" --timeout=180s

echo
echo "=== [8/8] Pods after restart ==="
kubectl -n "${NS}" get pods -o wide | grep -E "amf|ueransim-gnb|ueransim-ue" || true

echo
echo "=== AMF logs, last 5 minutes, filtered ==="

AMF_POD="$(kubectl -n "${NS}" get pods --sort-by=.metadata.creationTimestamp \
  -o jsonpath='{range .items[*]}{.metadata.name}{"\n"}{end}' \
  | grep "${AMF_DEPLOY}" \
  | tail -1)"

echo "AMF pod: ${AMF_POD}"

kubectl -n "${NS}" logs "${AMF_POD}" -c "${AMF_CONTAINER}" --since=5m | egrep -i "${LOG_FILTER}" || true

echo
echo "=== Now following AMF logs live. Press Ctrl+C to stop. ==="
kubectl -n "${NS}" logs "${AMF_POD}" -c "${AMF_CONTAINER}" -f | egrep -i "${LOG_FILTER}"
