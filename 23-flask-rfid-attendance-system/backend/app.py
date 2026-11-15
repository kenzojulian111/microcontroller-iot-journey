"""
===============================================================================
Project 23: Smart Access Control & SQLAlchemy Attendance System
File: backend/app.py
===============================================================================
Description:
    Production-grade Flask + Flask-SQLAlchemy ORM backend for an ESP32/MFRC522
    RFID attendance & physical access control system.

Key Features:
    - Relational SQLite schema (`Employee` and `AttendanceLog`) managed via
      SQLAlchemy 2.0 ORM.
    - Automatic stateful shift toggling (`CHECK_IN` <-> `CHECK_OUT`) with
      precise shift duration calculation (in hours and seconds) on check-out.
    - Auditing of unauthorized/unregistered badge taps (`DENIED` events) for
      security compliance.
    - RESTful Employee Card Enrollment API (`POST /api/v1/employees`) and
      activation toggle (`PATCH /api/v1/employees/<id>/toggle`).
    - Streamed RFC-4180 CSV attendance report export (`GET /api/v1/attendance/export.csv`).
    - Real-time operator dashboard (`GET /`) with one-click simulated card tap
      testing, badge provisioning modal, and live attendance ledger.
===============================================================================
"""

from __future__ import annotations

import csv
import io
import os
from datetime import datetime, timedelta, timezone
from typing import Any, Dict, List, Optional

from flask import Flask, Response, jsonify, render_template, request
from flask_sqlalchemy import SQLAlchemy
from sqlalchemy import desc, func

BASE_DIR = os.path.abspath(os.path.dirname(__file__))
DB_PATH = os.path.join(BASE_DIR, "attendance.db")

app = Flask(__name__)
app.config["SQLALCHEMY_DATABASE_URI"] = os.environ.get(
    "DATABASE_URL", f"sqlite:///{DB_PATH}"
)
app.config["SQLALCHEMY_TRACK_MODIFICATIONS"] = False
app.config["JSON_SORT_KEYS"] = False

db = SQLAlchemy(app)


# =============================================================================
# SQLAlchemy ORM Relational Models
# =============================================================================
class Employee(db.Model):
    __tablename__ = "employees"

    id = db.Column(db.Integer, primary_key=True)
    uid = db.Column(db.String(32), unique=True, nullable=False, index=True)
    full_name = db.Column(db.String(96), nullable=False)
    role = db.Column(db.String(64), nullable=False)
    department = db.Column(db.String(64), nullable=False, default="Engineering")
    is_active = db.Column(db.Boolean, nullable=False, default=True)
    currently_checked_in = db.Column(db.Boolean, nullable=False, default=False)
    last_check_in_at = db.Column(db.DateTime, nullable=True)
    created_at = db.Column(
        db.DateTime, nullable=False, default=lambda: datetime.now(timezone.utc)
    )

    logs = db.relationship(
        "AttendanceLog",
        back_populates="employee",
        lazy="dynamic",
        cascade="all, delete-orphan",
    )

    def to_dict(self) -> Dict[str, Any]:
        return {
            "id": self.id,
            "uid": self.uid,
            "full_name": self.full_name,
            "role": self.role,
            "department": self.department,
            "is_active": self.is_active,
            "currently_checked_in": self.currently_checked_in,
            "last_check_in_at": (
                self.last_check_in_at.strftime("%Y-%m-%d %H:%M:%S")
                if self.last_check_in_at
                else None
            ),
            "created_at": self.created_at.strftime("%Y-%m-%d %H:%M:%S"),
        }


class AttendanceLog(db.Model):
    __tablename__ = "attendance_logs"

    id = db.Column(db.Integer, primary_key=True)
    uid = db.Column(db.String(32), nullable=False, index=True)
    employee_id = db.Column(
        db.Integer, db.ForeignKey("employees.id"), nullable=True, index=True
    )
    terminal_id = db.Column(db.String(32), nullable=False, default="GATE-01")
    action = db.Column(
        db.String(24), nullable=False
    )  # CHECK_IN | CHECK_OUT | DENIED_REVOKED | DENIED_UNKNOWN
    status = db.Column(db.String(16), nullable=False)  # allowed | denied
    shift_seconds = db.Column(db.Integer, nullable=True)
    rssi = db.Column(db.Integer, nullable=True)
    punched_at = db.Column(
        db.DateTime,
        nullable=False,
        default=lambda: datetime.now(timezone.utc),
        index=True,
    )

    employee = db.relationship("Employee", back_populates="logs")

    def to_dict(self) -> Dict[str, Any]:
        shift_hours = (
            round(self.shift_seconds / 3600.0, 2)
            if self.shift_seconds is not None
            else None
        )
        return {
            "id": self.id,
            "uid": self.uid,
            "employee_id": self.employee_id,
            "user": self.employee.full_name if self.employee else "Unregistered Card",
            "role": self.employee.role if self.employee else "Unauthorized",
            "department": self.employee.department if self.employee else "N/A",
            "terminal_id": self.terminal_id,
            "action": self.action,
            "status": self.status,
            "shift_seconds": self.shift_seconds,
            "shift_hours": shift_hours,
            "rssi": self.rssi,
            "timestamp": self.punched_at.strftime("%H:%M:%S"),
            "date": self.punched_at.strftime("%Y-%m-%d"),
            "iso_timestamp": self.punched_at.isoformat(),
        }


# =============================================================================
# Database Initialization & Deterministic Seed Data
# =============================================================================
def normalize_uid(raw_uid: str) -> str:
    cleaned = raw_uid.strip().upper().replace("-", ":").replace(" ", ":")
    return cleaned


def seed_initial_records() -> None:
    if Employee.query.count() > 0:
        return

    now = datetime.now(timezone.utc)
    default_staff = [
        Employee(
            uid="04:A3:9B:12",
            full_name="Kenzo Julian",
            role="Lead Engineer",
            department="Embedded R&D",
            is_active=True,
            currently_checked_in=True,
            last_check_in_at=now - timedelta(hours=3, minutes=20),
        ),
        Employee(
            uid="A1:7F:44:C8",
            full_name="Elena Vance",
            role="Security Architect",
            department="Cyber-Physical Systems",
            is_active=True,
            currently_checked_in=False,
        ),
        Employee(
            uid="7B:2E:90:55",
            full_name="Marcus Chen",
            role="Firmware QA",
            department="Validation Lab",
            is_active=True,
            currently_checked_in=False,
        ),
        Employee(
            uid="C9:11:08:FA",
            full_name="Devon Mercer",
            role="Contractor",
            department="Facilities",
            is_active=False,  # Revoked badge example
            currently_checked_in=False,
        ),
    ]
    db.session.add_all(default_staff)
    db.session.flush()

    initial_logs = [
        AttendanceLog(
            uid="04:A3:9B:12",
            employee_id=default_staff[0].id,
            terminal_id="GATE-01",
            action="CHECK_IN",
            status="allowed",
            rssi=-49,
            punched_at=now - timedelta(hours=3, minutes=20),
        ),
        AttendanceLog(
            uid="A1:7F:44:C8",
            employee_id=default_staff[1].id,
            terminal_id="GATE-01",
            action="CHECK_IN",
            status="allowed",
            rssi=-52,
            punched_at=now - timedelta(hours=9, minutes=15),
        ),
        AttendanceLog(
            uid="A1:7F:44:C8",
            employee_id=default_staff[1].id,
            terminal_id="GATE-01",
            action="CHECK_OUT",
            status="allowed",
            shift_seconds=int(8.25 * 3600),
            rssi=-51,
            punched_at=now - timedelta(hours=1, minutes=0),
        ),
    ]
    db.session.add_all(initial_logs)
    db.session.commit()


with app.app_context():
    db.create_all()
    seed_initial_records()


# =============================================================================
# Web UI Route
# =============================================================================
@app.route("/", methods=["GET"])
def index() -> str:
    return render_template("index.html")


# =============================================================================
# REST API Endpoints
# =============================================================================
@app.route("/api/v1/attendance/punch", methods=["POST"])
def punch_attendance() -> tuple[Response, int]:
    """
    Processes an RFID tap event from an ESP32/ESP8266 MFRC522 gate terminal.
    Automatically toggles between CHECK_IN and CHECK_OUT and computes shift duration.
    """
    payload = request.get_json(silent=True) or {}
    raw_uid = str(payload.get("uid", "")).strip()
    terminal_id = str(payload.get("terminal_id", "GATE-01")).strip() or "GATE-01"
    rssi = payload.get("rssi", None)

    if not raw_uid:
        return (
            jsonify(
                {
                    "status": "error",
                    "message": "Missing required 'uid' field in JSON body.",
                }
            ),
            400,
        )

    uid = normalize_uid(raw_uid)
    now = datetime.now(timezone.utc)
    employee: Optional[Employee] = Employee.query.filter_by(uid=uid).first()

    # Case 1: Unregistered UID
    if employee is None:
        log_entry = AttendanceLog(
            uid=uid,
            employee_id=None,
            terminal_id=terminal_id,
            action="DENIED_UNKNOWN",
            status="denied",
            rssi=rssi,
            punched_at=now,
        )
        db.session.add(log_entry)
        db.session.commit()

        return (
            jsonify(
                {
                    "status": "denied",
                    "action": "DENIED",
                    "uid": uid,
                    "user": "Unregistered Card",
                    "role": "Unauthorized",
                    "terminal_id": terminal_id,
                    "timestamp": now.strftime("%H:%M:%S"),
                }
            ),
            403,
        )

    # Case 2: Employee exists but badge is deactivated/revoked
    if not employee.is_active:
        log_entry = AttendanceLog(
            uid=uid,
            employee_id=employee.id,
            terminal_id=terminal_id,
            action="DENIED_REVOKED",
            status="denied",
            rssi=rssi,
            punched_at=now,
        )
        db.session.add(log_entry)
        db.session.commit()

        return (
            jsonify(
                {
                    "status": "denied",
                    "action": "REVOKED",
                    "uid": uid,
                    "user": employee.full_name,
                    "role": employee.role,
                    "terminal_id": terminal_id,
                    "timestamp": now.strftime("%H:%M:%S"),
                }
            ),
            403,
        )

    # Case 3: Authorized Employee -> Toggle CHECK_IN / CHECK_OUT
    shift_seconds: Optional[int] = None
    shift_hours_str = ""

    if not employee.currently_checked_in:
        action = "CHECK_IN"
        employee.currently_checked_in = True
        employee.last_check_in_at = now
    else:
        action = "CHECK_OUT"
        if employee.last_check_in_at:
            last_in = employee.last_check_in_at
            if last_in.tzinfo is None:
                last_in = last_in.replace(tzinfo=timezone.utc)
            delta = now - last_in
            shift_seconds = max(1, int(delta.total_seconds()))
            shift_hours_str = f"{shift_seconds / 3600.0:.2f}"
        employee.currently_checked_in = False
        employee.last_check_in_at = None

    log_entry = AttendanceLog(
        uid=uid,
        employee_id=employee.id,
        terminal_id=terminal_id,
        action=action,
        status="allowed",
        shift_seconds=shift_seconds,
        rssi=rssi,
        punched_at=now,
    )
    db.session.add(log_entry)
    db.session.commit()

    response_payload: Dict[str, Any] = {
        "status": "allowed",
        "action": action,
        "uid": uid,
        "user": employee.full_name,
        "role": employee.role,
        "department": employee.department,
        "terminal_id": terminal_id,
        "timestamp": now.strftime("%H:%M:%S"),
    }
    if shift_seconds is not None:
        response_payload["shift_seconds"] = shift_seconds
        response_payload["shift_hours"] = shift_hours_str

    return jsonify(response_payload), 200


@app.route("/api/v1/employees", methods=["GET"])
def list_employees() -> Response:
    employees: List[Employee] = Employee.query.order_by(Employee.id.asc()).all()
    return jsonify({"count": len(employees), "employees": [e.to_dict() for e in employees]})


@app.route("/api/v1/employees", methods=["POST"])
def enroll_employee() -> tuple[Response, int]:
    """Enrolls a new RFID UID badge and binds it to an Employee ORM record."""
    payload = request.get_json(silent=True) or {}
    raw_uid = str(payload.get("uid", "")).strip()
    full_name = str(payload.get("full_name", "")).strip()
    role = str(payload.get("role", "")).strip()
    department = str(payload.get("department", "Engineering")).strip() or "Engineering"

    if not raw_uid or not full_name or not role:
        return (
            jsonify(
                {
                    "status": "error",
                    "message": "Fields 'uid', 'full_name', and 'role' are required.",
                }
            ),
            400,
        )

    uid = normalize_uid(raw_uid)
    existing = Employee.query.filter_by(uid=uid).first()
    if existing:
        return (
            jsonify(
                {
                    "status": "error",
                    "message": f"UID {uid} is already assigned to {existing.full_name}.",
                }
            ),
            409,
        )

    new_emp = Employee(
        uid=uid,
        full_name=full_name,
        role=role,
        department=department,
        is_active=True,
        currently_checked_in=False,
    )
    db.session.add(new_emp)
    db.session.commit()

    return jsonify({"status": "enrolled", "employee": new_emp.to_dict()}), 201


@app.route("/api/v1/employees/<int:emp_id>/toggle", methods=["PATCH"])
def toggle_employee_active(emp_id: int) -> tuple[Response, int]:
    emp = db.session.get(Employee, emp_id)
    if not emp:
        return jsonify({"status": "error", "message": "Employee not found."}), 404

    emp.is_active = not emp.is_active
    db.session.commit()
    return jsonify({"status": "updated", "employee": emp.to_dict()}), 200


@app.route("/api/v1/attendance/logs", methods=["GET"])
def get_attendance_logs() -> Response:
    limit = min(int(request.args.get("limit", 50)), 250)
    logs: List[AttendanceLog] = (
        AttendanceLog.query.order_by(desc(AttendanceLog.punched_at)).limit(limit).all()
    )

    total_employees = Employee.query.filter_by(is_active=True).count()
    currently_on_site = Employee.query.filter_by(
        is_active=True, currently_checked_in=True
    ).count()
    denied_count = AttendanceLog.query.filter_by(status="denied").count()
    avg_shift_sec = (
        db.session.query(func.avg(AttendanceLog.shift_seconds))
        .filter(AttendanceLog.shift_seconds.isnot(None))
        .scalar()
    )
    avg_shift_hours = round((avg_shift_sec or 0) / 3600.0, 2)

    return jsonify(
        {
            "metrics": {
                "active_employees": total_employees,
                "currently_on_site": currently_on_site,
                "denied_attempts": denied_count,
                "avg_shift_hours": avg_shift_hours,
                "total_events": AttendanceLog.query.count(),
            },
            "logs": [log.to_dict() for log in logs],
        }
    )


@app.route("/api/v1/attendance/export.csv", methods=["GET"])
def export_attendance_csv() -> Response:
    """Streams an RFC-4180 CSV attendance audit report."""
    logs: List[AttendanceLog] = AttendanceLog.query.order_by(
        desc(AttendanceLog.punched_at)
    ).all()

    output = io.StringIO()
    writer = csv.writer(output)
    writer.writerow(
        [
            "Log ID",
            "UTC Timestamp",
            "Terminal ID",
            "RFID UID",
            "Employee Name",
            "Role",
            "Department",
            "Action",
            "Status",
            "Shift Duration (Hours)",
            "Wi-Fi RSSI (dBm)",
        ]
    )

    for entry in logs:
        data = entry.to_dict()
        writer.writerow(
            [
                data["id"],
                f"{data['date']} {data['timestamp']}",
                data["terminal_id"],
                data["uid"],
                data["user"],
                data["role"],
                data["department"],
                data["action"],
                data["status"],
                data["shift_hours"] if data["shift_hours"] is not None else "",
                data["rssi"] if data["rssi"] is not None else "",
            ]
        )

    csv_bytes = output.getvalue()
    return Response(
        csv_bytes,
        mimetype="text/csv",
        headers={
            "Content-Disposition": "attachment; filename=attendance_audit_report.csv"
        },
    )


if __name__ == "__main__":
    port = int(os.environ.get("PORT", 5023))
    app.run(host="0.0.0.0", port=port, debug=False)
