import os

Import("env")

secrets = os.path.join(env["PROJECT_DIR"], "secrets.ini")
if not os.path.exists(secrets):
    open(secrets, "w").close()
