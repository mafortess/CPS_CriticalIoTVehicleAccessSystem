from flask import Flask, render_template
from flask_login import LoginManager
from jinja2 import TemplateNotFound
from .database.bigquery_db import BigQueryDB
import os
from dotenv import load_dotenv

# Initialize extensions
login_manager = LoginManager()
db = None  # Will be initialized with BigQueryDB instance

def create_app():
    load_dotenv()  # Load environment variables from .env file if it exists

    app = Flask(__name__)
    app.config['SECRET_KEY'] = os.getenv('SECRET_KEY', 'dev-key-change-this')
    
    # Initialize BigQuery connection
    global db
    project_id = os.getenv('GOOGLE_CLOUD_PROJECT')
    if not project_id:
        raise ValueError("GOOGLE_CLOUD_PROJECT environment variable must be set")
   
    # Initialize BigQuery connection safely
    try:
        db = BigQueryDB(project_id) #ahora solo crea objeto, no se conecta
        app.config["CLOUD_AVAILABLE"] = True
        print("Cloud available")
    except Exception as e:
        app.config["CLOUD_AVAILABLE"] = False
        print("Cloud NOT available")
    # Initialize login manager with stricter settings
    login_manager.init_app(app)
    login_manager.login_view = 'auth.login'
    login_manager.login_message = 'Please log in to access this page.'
    login_manager.login_message_category = 'warning'
    login_manager.session_protection = 'strong'

    # Register error handlers
    @app.errorhandler(404)
    @app.errorhandler(TemplateNotFound)
    def page_not_found(e):
        return render_template('errors/404.html'), 404

    with app.app_context():
        # Import parts that need the initialized extensions
        from .mqtt_handler import init_mqtt
        from .controllers.auth import auth_bp
        from .controllers.main import main_bp
        
        # Initialize MQTT after database is ready
        init_mqtt(app)

        # Register blueprints
        app.register_blueprint(auth_bp)
        app.register_blueprint(main_bp)

        if app.config["CLOUD_AVAILABLE"] == True:
            # Create admin user if needed
            create_admin_if_not_exists(app)

    return app

def create_admin_if_not_exists(app):
    """Create default admin user if no users exist"""
    try:
        if not db.has_users():
            print("No users found, creating default admin user...")
            from werkzeug.security import generate_password_hash
            success = db.create_user(
                username="admin",
                password_hash=generate_password_hash(os.getenv('ADMIN_PASSWORD')),
                is_admin=True
            )
            if success:
                print("Default admin user created")
            else:
                print("Error creating default admin user")
    except Exception as e:
        print(f"Error checking/creating admin user: {str(e)}")