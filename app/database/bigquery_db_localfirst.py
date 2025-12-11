from google.cloud import bigquery
from datetime import datetime
import uuid

class BigQueryDB:
    def __init__(self, project_id):
        #self.client = bigquery.Client(project=project_id) # no hacer aquí, BLOQUEANTE
        self.client = None # bigquery.Client(project=project_id) # no hacer aquí, BLOQUEANTE
        self.dataset_id = "IoT2"
        self.tables = {
            "User": "User",
            "Vehicle": "Vehicle",
            "AccessLog": "AccessLog",
            "Gate": "Gate"
        }

    def get_table_ref(self, table_name):
        return f"{self.client.project}.{self.dataset_id}.{self.tables[table_name]}"    # User operations
    def get_user_by_username(self, username):
        query = f"""
        SELECT id, username, password, is_admin, created_at 
        FROM `{self.get_table_ref('User')}`
        WHERE username = @username
        """
        job_config = bigquery.QueryJobConfig(
            query_parameters=[
                bigquery.ScalarQueryParameter("username", "STRING", username)
            ]
        )
        query_job = self.client.query(query, job_config=job_config)
        results = list(query_job.result())
        
        if results:
            return results[0]  # Devolver el Row directamente
        return None
    
    def get_user_by_id(self, user_id):
        query = f"""
        SELECT id, username, password, is_admin, created_at 
        FROM `{self.get_table_ref('User')}`
        WHERE id = @user_id
        """
        job_config = bigquery.QueryJobConfig(
            query_parameters=[
                bigquery.ScalarQueryParameter("user_id", "STRING", user_id)
            ]
        )
        try:
            query_job = self.client.query(query, job_config=job_config)
            results = list(query_job.result())
            if results:
                return results[0]  # Devolver el Row directamente
            return None
        except Exception as e:
            print(f"BigQueryDB: Error querying user {user_id}: {e}")
            return None

    def create_user(self, username, password_hash, is_admin=False):
        table_ref = self.get_table_ref('User')
        rows_to_insert = [{
            'id': str(uuid.uuid4()),
            'username': username,
            'password': str(password_hash),
            'is_admin': is_admin,
            'created_at': datetime.now().isoformat()
        }]

        errors = self.client.insert_rows_json(table_ref, rows_to_insert)
        if errors:
            print(f"BigQuery insert error: {errors}")
        return len(errors) == 0


    def has_users(self):
        """Check if there are any users in the database"""
        query = f"SELECT COUNT(*) as count FROM `{self.get_table_ref('User')}`"
        result = next(self.client.query(query).result())
        return result.count > 0

    # Vehicle operations
    def get_vehicles(self):
        query = f"""
        SELECT * FROM `{self.get_table_ref('Vehicle')}`
        """
        return list(self.client.query(query).result())

    def get_vehicle_by_plate(self, plate_number):
        query = f"""
        SELECT * FROM `{self.get_table_ref('Vehicle')}`
        WHERE plate_number = @plate
        """
        job_config = bigquery.QueryJobConfig(
            query_parameters=[
                bigquery.ScalarQueryParameter("plate", "STRING", plate_number)
            ]
        )
        results = list(self.client.query(query, job_config=job_config).result())
        return results[0] if results else None

    def list_vehicles(self):
        """Get all vehicles"""
        query = f"SELECT * FROM `{self.get_table_ref('Vehicle')}`"
        results = list(self.client.query(query).result())
        return results

    def add_vehicle(self, plate_number, owner_name, valid_from, valid_until=None, is_authorized=True):
        """Add a new vehicle to the database"""
        try:
            table_ref = self.get_table_ref('Vehicle')
            rows_to_insert = [{
                'id': str(uuid.uuid4()),
                'plate_number': plate_number,
                'owner_name': owner_name,
                'is_authorized': is_authorized,
                'valid_from': valid_from.isoformat(),
                'valid_until': valid_until.isoformat() if valid_until else None,
                'last_sync': datetime.now().isoformat()
            }]
            
            errors = self.client.insert_rows_json(table_ref, rows_to_insert)
            return len(errors) == 0, errors
        except Exception as e:
            return False, str(e)

    def get_paginated_access_logs(self, page=1, per_page=20, sort_by='timestamp', sort_order='desc'):
        """Get paginated access logs with sorting"""
        # Calculate offset
        offset = (page - 1) * per_page

        # Validate sort parameters
        allowed_sort_fields = {'timestamp': 'timestamp', 'access_granted': 'access_granted'}
        sort_field = allowed_sort_fields.get(sort_by, 'timestamp')
        order = 'DESC' if sort_order.lower() == 'desc' else 'ASC'

        # Get total count
        count_query = f"SELECT COUNT(*) as total FROM `{self.get_table_ref('AccessLog')}`"
        total_count = next(self.client.query(count_query).result()).total

        # Get paginated and sorted results
        query = f"""
        SELECT *
        FROM `{self.get_table_ref('AccessLog')}`
        ORDER BY {sort_field} {order}
        LIMIT @per_page
        OFFSET @offset
        """
        
        job_config = bigquery.QueryJobConfig(
            query_parameters=[
                bigquery.ScalarQueryParameter("per_page", "INT64", per_page),
                bigquery.ScalarQueryParameter("offset", "INT64", offset)
            ]
        )
        
        results = list(self.client.query(query, job_config=job_config).result())
        
        # Calculate pagination metadata
        total_pages = -(-total_count // per_page)  # Ceiling division
        has_prev = page > 1
        has_next = page < total_pages

        # Create Pagination object
        pagination = {
            'items': results,
            'page': page,
            'per_page': per_page,
            'total': total_count,
            'pages': total_pages,
            'has_prev': has_prev,
            'has_next': has_next,
            'prev_num': page - 1 if has_prev else None,
            'next_num': page + 1 if has_next else None,
            'iter_pages': lambda left_edge=2, left_current=2, right_current=3, right_edge=2: self._iter_pages(
                page, total_pages, left_edge, left_current, right_current, right_edge
            )
        }
        
        return type('Pagination', (), pagination)  # Create object with pagination attributes

    def _iter_pages(self, curr_page, num_pages, left_edge=2, left_current=2, right_current=3, right_edge=2):
        """Helper function to generate page numbers for pagination"""
        last = 0
        for num in range(1, num_pages + 1):
            if num <= left_edge or \
               (num > curr_page - left_current - 1 and num < curr_page + right_current) or \
               num > num_pages - right_edge:
                if last + 1 != num:
                    yield None
                yield num
                last = num

    # AccessLog operations
    def create_access_log(self, id, plate_number, gate_id, access_granted, confidence_score=None, timestamp=None, accessing=False):
        table_ref = self.get_table_ref('AccessLog')
        
        rows_to_insert = [{
            'id': id,
            'plate_number': plate_number,
            'gate_id': gate_id,
            'access_granted': access_granted,
            'confidence_score': confidence_score,
            'timestamp': timestamp if timestamp else datetime.now().isoformat(),
            'accessing': True if accessing else False
        }]
        # Insert the log 
        errors = self.client.insert_rows_json(table_ref, rows_to_insert)

        return len(errors) == 0

    def get_access_logs(self, gate_id=None, limit=100):
        query = f"""
        SELECT * FROM `{self.get_table_ref('AccessLog')}`
        {f"WHERE gate_id = @gate_id" if gate_id else ""}
        ORDER BY timestamp DESC
        LIMIT @limit
        """
        job_config = bigquery.QueryJobConfig(
            query_parameters=[
                bigquery.ScalarQueryParameter("limit", "INTEGER", limit)
            ]
        )
        if gate_id:
            job_config.query_parameters.append(
                bigquery.ScalarQueryParameter("gate_id", "STRING", gate_id)
            )
        return list(self.client.query(query, job_config=job_config).result())

    # Gate operations
    def get_gate(self, gate_id):
        query = f"""
        SELECT * FROM `{self.get_table_ref('Gate')}`
        WHERE gate_id = @gate_id
        """
        job_config = bigquery.QueryJobConfig(
            query_parameters=[
                bigquery.ScalarQueryParameter("gate_id", "STRING", gate_id)
            ]
        )
        results = list(self.client.query(query, job_config=job_config).result())
        return results[0] if results else None    
        
    def update_gate_status(self, gate_id, status, last_online=None):
        """Update a gate's status and last online timestamp"""
        try:
            table_ref = self.get_table_ref('Gate')
            # Construir la query base
            query = f"""
            UPDATE `{table_ref}`
            SET status = @status"""
            
            # Configurar parámetros base
            parameters = [
                bigquery.ScalarQueryParameter("status", "STRING", status),
                bigquery.ScalarQueryParameter("gate_id", "STRING", gate_id)
            ]
            
            # Añadir last_online si se proporciona
            if last_online:
                query += ", last_online = @last_online"
                parameters.append(
                    bigquery.ScalarQueryParameter("last_online", "DATETIME", last_online.isoformat())
                )
            
            # Completar la query
            query += "\nWHERE gate_id = @gate_id"
            
            # Ejecutar la consulta
            job_config = bigquery.QueryJobConfig(query_parameters=parameters)
            query_job = self.client.query(query, job_config=job_config)
            query_job.result()
            return True
        except Exception as e:
            print(f"Error updating gate status: {e}")
            return False

    def list_gates(self):
        """Get all gates"""
        query = f"SELECT * FROM `{self.get_table_ref('Gate')}`"
        return list(self.client.query(query).result())

    def add_gate(self, gate_id, location):
        """Add a new gate"""
        table_ref = self.get_table_ref('Gate')
        rows_to_insert = [{
            'id': str(uuid.uuid4()),
            'gate_id': gate_id,
            'location': location,
            'status': 'offline'
        }]
        
        errors = self.client.insert_rows_json(table_ref, rows_to_insert)
        return len(errors) == 0, errors

    def delete_gate(self, id):
        """Delete a gate by its ID"""
        try:
            delete_query = f"""
            DELETE FROM `{self.get_table_ref('Gate')}`
            WHERE id = @id
            """
            job_config = bigquery.QueryJobConfig(
                query_parameters=[
                    bigquery.ScalarQueryParameter("id", "STRING", id)
                ]
            )
            query_job = self.client.query(delete_query, job_config=job_config)
            query_job.result()
            return True
        except Exception as e:
            return False

    def sync_gate(self, gate_id):
        """Update gate's local_cache_updated timestamp"""
        try:
            update_query = f"""
            UPDATE `{self.get_table_ref('Gate')}`
            SET local_cache_updated = @local_cache_updated
            WHERE gate_id = @gate_id
            """
            job_config = bigquery.QueryJobConfig(
                query_parameters=[
                    bigquery.ScalarQueryParameter("local_cache_updated", "DATETIME", datetime.now().isoformat()),
                    bigquery.ScalarQueryParameter("gate_id", "STRING", gate_id)
                ]
            )
            query_job = self.client.query(update_query, job_config=job_config)
            query_job.result()
            return True
        except Exception as e:
            return False

    def update_vehicle(self, plate_number, owner_name, is_authorized, valid_from, valid_until=None):
        """Update an existing vehicle"""
        try:
            update_query = f"""
            UPDATE `{self.get_table_ref('Vehicle')}`
            SET owner_name = @owner_name,
                is_authorized = @is_authorized,
                valid_from = @valid_from,
                valid_until = @valid_until,
                last_sync = @last_sync
            WHERE plate_number = @plate_number
            """
            
            job_config = bigquery.QueryJobConfig(
                query_parameters=[
                    bigquery.ScalarQueryParameter("owner_name", "STRING", owner_name),
                    bigquery.ScalarQueryParameter("is_authorized", "BOOL", is_authorized),
                    bigquery.ScalarQueryParameter("valid_from", "DATETIME", valid_from),
                    bigquery.ScalarQueryParameter("valid_until", "DATETIME", valid_until if valid_until else None),
                    bigquery.ScalarQueryParameter("last_sync", "DATETIME", datetime.now().isoformat()),
                    bigquery.ScalarQueryParameter("plate_number", "STRING", plate_number)
                ]
            )
            
            query_job = self.client.query(update_query, job_config=job_config)
            query_job.result()
            return True, None
        except Exception as e:
            return False, str(e)

    def get_dashboard_stats(self):
        """Get statistics for the dashboard"""
        try:
            # Query for total number of vehicles
            vehicle_count_query = f"""
            SELECT COUNT(*) as count 
            FROM `{self.get_table_ref('Vehicle')}`
            WHERE is_authorized = TRUE
            """
            vehicle_count = next(self.client.query(vehicle_count_query).result()).count

            # Query for today's access attempts
            today_attempts_query = f"""
            SELECT 
                COUNT(*) as total_attempts,
                COUNTIF(access_granted = TRUE) as successful_attempts
            FROM `{self.get_table_ref('AccessLog')}`
            WHERE DATE(timestamp) = CURRENT_DATE()
            """
            attempts_stats = next(self.client.query(today_attempts_query).result())

            # Query for gates status
            gates_status_query = f"""
            SELECT 
                COUNT(*) as total_gates,
                COUNTIF(status = 'online') as online_gates
            FROM `{self.get_table_ref('Gate')}`
            """
            gates_stats = next(self.client.query(gates_status_query).result())

            return {
                'total_vehicles': vehicle_count,
                'total_attempts_today': attempts_stats.total_attempts,
                'successful_attempts_today': attempts_stats.successful_attempts,
                'total_gates': gates_stats.total_gates,
                'online_gates': gates_stats.online_gates
            }
        except Exception as e:
            print(f"Error getting dashboard stats: {e}")
            return {
                'total_vehicles': 0,
                'total_attempts_today': 0,
                'successful_attempts_today': 0,
                'total_gates': 0,
                'online_gates': 0
            }

    def get_sync_info(self):
        """Get the current sync information for vehicles"""
        try:
            query = f"""
            SELECT MAX(CAST(last_sync as STRING)) as max_sync,
                   COUNT(*) as total_vehicles
            FROM `{self.get_table_ref('Vehicle')}`
            """
            result = next(self.client.query(query).result())
            
            # Use the hash of last_sync and total vehicles as version
            sync_version = hash(f"{result.max_sync}_{result.total_vehicles}") % 1000000
            return {
                'sync_version': sync_version,
                'last_sync': result.max_sync
            }
        except Exception as e:
            print(f"Error getting sync info: {e}")
            return {
                'sync_version': 0,
                'last_sync': datetime.utcnow().isoformat()
            }