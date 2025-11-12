from flask import Flask, render_template, request, jsonify, send_from_directory
import os
import csv
from datetime import datetime

app = Flask(__name__)
values = dict()

sensor_data_labels = ['temperature', 'humidity', 'pressure', 'gasResistance', 'lpg', 'ch4', 'smoke']
# deg C, %, kPa, KOhm

def log(msg):
    timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
    print(f"[{timestamp}] {msg}")


@app.route('/update', methods=['POST'])
def update_data():
    try:
        # Get the new value from the request JSON body
        recieved_sensor_data = request.get_json()
        if 'name' not in recieved_sensor_data:
            return jsonify({'error': 'No name provided'}), 400
        
        for data_name in sensor_data_labels:
            if data_name not in recieved_sensor_data:
                return jsonify({'error': f'No value provided for {data_name}'}), 400
            values[recieved_sensor_data['name']]['data'][data_name] = recieved_sensor_data[data_name]

        values[recieved_sensor_data['name']]['last_upd'] = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        log(f"Updated data for {recieved_sensor_data['name']}: {values[recieved_sensor_data['name']]['data']}")

        # Append the new data to the CSV file
        file_present = os.path.exists(f'data/data_{recieved_sensor_data['name']}.csv')

        with open(f'data/data_{recieved_sensor_data['name']}.csv', mode='a', newline='') as file:
            if not file_present:
                writer = csv.writer(file)
                header = ['Timestamp', 'Temperature (°C)', 'Humidity (%)', 'Pressure (kPa)', 'Gas Resistance (KOhm)']
                writer.writerow(header)
            writer = csv.writer(file)
            row = [values[recieved_sensor_data['name']]['last_upd']] + [values[recieved_sensor_data['name']]['data'][name] for name in sensor_data_labels]
            writer.writerow(row)

        return jsonify({'message': f'Data updated successfully'}), 200
    except Exception as e:
        log(f'Error in update_data: {e}')
        return jsonify({'error': 'Invalid Query'}), 400


@app.route('/register', methods=['POST'])
def register_sensor():
    try:
        # Get the new value from the request JSON body
        data = request.get_json()
        dev_name = str(data.get('name'))
        dev_loc = str(data.get('location'))
        values[dev_name] = {
            'name' : dev_name,
            'data' : {
                'temperature' : 0.0,
                'humidity' : 0.0,
                'pressure' : 0.0,
                'gasResistance' : 0.0
            },
            'location' : dev_loc,
            'last_upd' : datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        }

        return jsonify({'message': 'Sensor Registered Successfully'}), 200
    except Exception as e:
        log(f'Error in register_sensor: {e}')
        return jsonify({'error': 'Invalid Query'}), 400


@app.route('/unregister', methods=['POST'])
def unregister():
    try:
        # Get the new value from the request JSON body
        data = request.get_json()
        del values[str(data.get('name'))]
        return jsonify({'message': 'Sensor Removed Successfully'}), 200
    except Exception as e:
        log(f'Error in unregister: {e}')
        return jsonify({'error': 'Invalid Query'}), 400


@app.route('/get_list', methods=['POST'])
def retrieve_list():
    try:
        return jsonify(values), 200
    except Exception as e:
        log(f'Error in retrieve_list: {e}')
        return jsonify({'error': 'Invalid Query'}), 400


if __name__ == '__main__':
    app.run(debug=True, host='0.0.0.0', port=5000)