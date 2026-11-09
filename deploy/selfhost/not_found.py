"""Static, credential-free 404 for document navigations; API errors stay untouched."""

from pathlib import Path
import base64
import hashlib


def response():
    body = Path(__file__).with_name("404.html").read_text(encoding="utf-8")
    css = body.split("<style>", 1)[1].split("</style>", 1)[0]
    digest = base64.b64encode(hashlib.sha256(css.encode()).digest()).decode()
    return {
        "handler": "static_response",
        "status_code": 404,
        "body": body,
        "headers": {
            "Content-Type": ["text/html; charset=utf-8"],
            "Cache-Control": ["no-store"],
            "X-Content-Type-Options": ["nosniff"],
            "Referrer-Policy": ["no-referrer"],
            "Content-Security-Policy": [
                f"default-src 'none'; style-src 'sha256-{digest}'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'"
            ],
        },
    }


def document_route(domain):
    return {
        "match": [
            {
                "host": [domain],
                "method": ["GET", "HEAD"],
                "header": {"Accept": ["*text/html*"]},
                "not": [
                    {
                        "path": [
                            "/",
                            "/index.html",
                            "/favicon.svg",
                            "/api",
                            "/api/*",
                            "/healthz",
                            "/assets/*",
                            "/effects/*",
                        ]
                    }
                ],
            }
        ],
        "handle": [response()],
        "terminal": True,
    }
