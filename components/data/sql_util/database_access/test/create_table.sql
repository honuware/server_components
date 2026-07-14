CREATE TABLE IF NOT EXISTS people (
  person_id SERIAL PRIMARY KEY,
  email VARCHAR,
  first_name VARCHAR,
  last_name VARCHAR,
  password_hash VARCHAR,
  UNIQUE (email)
)