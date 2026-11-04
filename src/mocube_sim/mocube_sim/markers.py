"""
RViz 用のマーカーを作る.

- 本当の位置: 塗りつぶした立方体と ID の文字。止まっているものは灰色、動いているものは橙、ID0 は濃い灰色
- 推定の位置 (/module_states): 立方体の枠線。本当の位置とのずれが目で見える
"""

from itertools import product

from geometry_msgs.msg import Point
from std_msgs.msg import ColorRGBA
from visualization_msgs.msg import Marker

STILL = ColorRGBA(r=0.75, g=0.78, b=0.82, a=0.9)
FIXED = ColorRGBA(r=0.35, g=0.37, b=0.40, a=0.9)
MOVING = ColorRGBA(r=1.0, g=0.55, b=0.1, a=0.95)
TEXT = ColorRGBA(r=1.0, g=1.0, b=1.0, a=1.0)
ESTIMATE = ColorRGBA(r=0.1, g=0.8, b=1.0, a=1.0)

MOVING_SPEED = 1e-5  # これより速ければ動いているとみなす [m/s]


def _cube_edges(half):
    """中心が原点、一辺 2 * half の立方体の 12 本の辺 (端点の組)."""
    corners = list(product((-half, half), repeat=3))
    edges = []
    for i, a in enumerate(corners):
        for b in corners[i + 1:]:
            if sum(x != y for x, y in zip(a, b)) == 1:
                edges.append((a, b))
    return edges


def true_markers(positions, velocities, edge_length, frame_id, stamp):
    """本当の位置 {ID: [x, y, z]} から、立方体と ID の文字のマーカーを作る."""
    markers = []
    size = edge_length * 0.96  # 隣どうしの境目が見えるよう少し小さく
    for module_id, position in sorted(positions.items()):
        center = Point(x=float(position[0]), y=float(position[1]), z=float(position[2]))
        speed = float(sum(v * v for v in velocities.get(module_id, (0.0, 0.0, 0.0))) ** 0.5)
        if speed > MOVING_SPEED:
            color = MOVING
        elif module_id == 0:
            color = FIXED
        else:
            color = STILL

        cube = Marker()
        cube.header.frame_id = frame_id
        cube.header.stamp = stamp
        cube.ns = 'true'
        cube.id = module_id
        cube.type = Marker.CUBE
        cube.action = Marker.ADD
        cube.pose.position = center
        cube.pose.orientation.w = 1.0
        cube.scale.x = cube.scale.y = cube.scale.z = size
        cube.color = color
        markers.append(cube)

        text = Marker()
        text.header.frame_id = frame_id
        text.header.stamp = stamp
        text.ns = 'id'
        text.id = module_id
        text.type = Marker.TEXT_VIEW_FACING
        text.action = Marker.ADD
        text.pose.position = Point(x=center.x, y=center.y, z=center.z + edge_length * 0.55)
        text.pose.orientation.w = 1.0
        text.scale.z = edge_length * 0.35
        text.color = TEXT
        text.text = str(module_id)
        markers.append(text)
    return markers


def estimate_markers(positions, edge_length, frame_id, stamp):
    """推定の位置 {ID: [x, y, z]} から、立方体の枠線のマーカーを 1 つ作る."""
    marker = Marker()
    marker.header.frame_id = frame_id
    marker.header.stamp = stamp
    marker.ns = 'estimate'
    marker.id = 0
    marker.type = Marker.LINE_LIST
    marker.action = Marker.ADD
    marker.pose.orientation.w = 1.0
    marker.scale.x = edge_length * 0.02  # 線の太さ
    marker.color = ESTIMATE
    if not positions:
        marker.action = Marker.DELETE
        return [marker]
    edges = _cube_edges(edge_length * 0.5)
    for position in positions.values():
        for a, b in edges:
            for corner in (a, b):
                marker.points.append(Point(
                    x=float(position[0] + corner[0]),
                    y=float(position[1] + corner[1]),
                    z=float(position[2] + corner[2])))
    return [marker]
