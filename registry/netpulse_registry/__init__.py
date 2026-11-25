import os

from flask import Flask

from netpulse_registry.db import close_db, init_db
from netpulse_registry.routes import bp


def create_app(db_path=None, seed=None):
    app = Flask(__name__, template_folder="templates")
    if db_path is None:
        db_path = os.environ.get("NETPULSE_DB", os.path.join(os.getcwd(), "netpulse.db"))
    if seed is None:
        seed = os.environ.get("NETPULSE_SEED", "").lower() in {"1", "true", "yes"}
    parent = os.path.dirname(os.path.abspath(db_path))
    if parent:
        os.makedirs(parent, exist_ok=True)

    app.config["DB_PATH"] = db_path
    app.config["NETPULSE_SEED"] = bool(seed)
    init_db(db_path, app.config["NETPULSE_SEED"])
    app.register_blueprint(bp)
    app.teardown_appcontext(close_db)
    return app
