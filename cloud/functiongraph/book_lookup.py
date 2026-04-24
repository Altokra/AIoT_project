# -*- coding: utf-8 -*-
"""
华为云 FunctionGraph 函数：ISBN 图书信息查询（使用 data.isbn.work API）
触发方式：IoTDA 规则引擎转发
流程：提取 ISBN → 查询 data.isbn.work API → 通过 IoTDA API 下发命令给设备

环境变量（在 FunctionGraph 控制台配置）：
  HW_DOMAIN_NAME  - 华为云账号名
  HW_USER_NAME    - IAM 用户名
  HW_PASSWORD     - 密码
  HW_PROJECT_ID   - 项目 ID（控制台 → 我的凭证 → 项目列表）
  IOTDA_REGION    - 区域，如 cn-east-3
  DEVICE_ID       - 设备 ID（如 69e97a34e094d61592351ea4_pLib）
"""

import json
import urllib.request
import urllib.error
import re
import ssl


def handler(event, context):
    logger = context.getLogger()
    logger.info("收到事件: " + json.dumps(event, ensure_ascii=False))

    # 1. 提取 ISBN
    isbn = extract_isbn(event)
    if not isbn:
        logger.info("未找到 ISBN，跳过")
        return {"statusCode": 200, "body": "No ISBN found"}

    logger.info("查询 ISBN: " + isbn)

    # 2. 查询 Open Library API
    book_info = query_open_library(isbn, logger)
    if not book_info:
        logger.info("未找到书籍信息")
        return {"statusCode": 200, "body": "Book not found for ISBN: " + isbn}

    logger.info("查询到书籍: " + book_info.get("title", ""))

    # 3. 通过 IoTDA API 下发命令给设备
    device_id = context.getUserData("DEVICE_ID")
    result = send_command_to_device(device_id, book_info, context, logger)

    return {
        "statusCode": 200,
        "body": json.dumps(
            {"result": result, "book": book_info}, ensure_ascii=False
        ),
    }


# ------------------------------------------------------------------
# 从 IoTDA 规则引擎转发的事件中提取 ISBN
# ------------------------------------------------------------------
def extract_isbn(event):
    if not isinstance(event, dict):
        return None

    # 格式 1: 规则引擎直接转发
    for svc in event.get("services", []):
        data = svc.get("data", {})
        if data.get("book_status") == "registered":
            return data.get("book_isbn")

    # 格式 2: 通过 DIS / OBS 转发（嵌套 records）
    for record in event.get("records", []):
        content = record.get("content", "{}")
        if isinstance(content, str):
            content = json.loads(content)
        for svc in content.get("services", []):
            data = svc.get("data", {})
            if data.get("book_status") == "registered":
                return data.get("book_isbn")

    return None


# ------------------------------------------------------------------
# 查询图书信息 API（data.isbn.work）
# ------------------------------------------------------------------
def query_open_library(isbn, logger):
    app_key = "ae1718d4587744b0b79f940fbef69e77"
    url = (
        "https://data.isbn.work/openApi/getInfoByIsbn"
        "?isbn=" + isbn + "&appKey=" + app_key
    )

    try:
        ctx = ssl.create_default_context()
        req = urllib.request.Request(
            url, headers={"User-Agent": "SmartLibrary/1.0"}
        )
        with urllib.request.urlopen(req, timeout=15, context=ctx) as resp:
            resp_data = json.loads(resp.read().decode("utf-8"))

        if resp_data.get("code") != 0 or not resp_data.get("success"):
            logger.error("API 返回错误: " + json.dumps(resp_data, ensure_ascii=False))
            return None

        book_data = resp_data.get("data", {})
        info = {
            "isbn": isbn,
            "title": book_data.get("bookName", ""),
            "author": book_data.get("author", ""),
            "publisher": book_data.get("press", ""),
            "year": 0,
        }

        # 提取出版年份
        press_date = book_data.get("pressDate", "")
        match = re.search(r"\d{4}", press_date)
        if match:
            info["year"] = int(match.group())

        return info

    except Exception as e:
        logger.error("查询图书 API 失败: " + str(e))
        return None


# ------------------------------------------------------------------
# 获取华为云 IAM Token
# ------------------------------------------------------------------
def get_iam_token(context, logger):
    domain = context.getUserData("HW_DOMAIN_NAME")
    user = context.getUserData("HW_USER_NAME")
    pwd = context.getUserData("HW_PASSWORD")
    region = context.getUserData("IOTDA_REGION") or "cn-east-3"

    if not all([domain, user, pwd]):
        logger.error("缺少 IAM 认证环境变量")
        return None

    body = {
        "auth": {
            "identity": {
                "methods": ["password"],
                "password": {
                    "user": {
                        "name": user,
                        "password": pwd,
                        "domain": {"name": domain},
                    }
                },
            },
            "scope": {"project": {"name": region}},
        }
    }

    try:
        ctx = ssl.create_default_context()
        req = urllib.request.Request(
            "https://iam.myhuaweicloud.com/v3/auth/tokens",
            data=json.dumps(body).encode("utf-8"),
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        with urllib.request.urlopen(req, timeout=10, context=ctx) as resp:
            return resp.headers.get("X-Subject-Token")
    except Exception as e:
        logger.error("获取 IAM Token 失败: " + str(e))
        return None


# ------------------------------------------------------------------
# 通过 IoTDA REST API 向设备下发命令
# ------------------------------------------------------------------
def send_command_to_device(device_id, book_info, context, logger):
    token = get_iam_token(context, logger)
    if not token:
        return "IAM auth failed"

    region = context.getUserData("IOTDA_REGION") or "cn-east-3"
    project_id = context.getUserData("HW_PROJECT_ID")

    url = (
        "https://iotda-client."
        + region
        + ".myhuaweicloud.com/v5/iot/"
        + project_id
        + "/devices/"
        + device_id
        + "/commands"
    )

    command = {
        "service_id": "Env",
        "command_name": "deliver_book_info",
        "paras": {
            "isbn": book_info["isbn"],
            "title": book_info["title"],
            "author": book_info["author"],
            "publisher": book_info["publisher"],
            "year": book_info["year"],
        },
    }

    try:
        ctx = ssl.create_default_context()
        req = urllib.request.Request(
            url,
            data=json.dumps(command).encode("utf-8"),
            headers={
                "Content-Type": "application/json",
                "X-Auth-Token": token,
            },
            method="POST",
        )
        with urllib.request.urlopen(req, timeout=10, context=ctx) as resp:
            result = json.loads(resp.read().decode("utf-8"))
            logger.info("命令下发成功: " + json.dumps(result, ensure_ascii=False))
            return "success"

    except urllib.error.HTTPError as e:
        body = e.read().decode("utf-8")
        logger.error("命令下发失败: " + str(e.code) + " " + body)
        return "failed: " + str(e.code)
    except Exception as e:
        logger.error("命令下发异常: " + str(e))
        return "error: " + str(e)
