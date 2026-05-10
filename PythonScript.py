import serial
import serial.tools.list_ports
import numpy as np
import matplotlib.pyplot as plt
import csv
import wave
import time
import os











# -------------------- Export as Wave file function --------------------
def save_normalised_adc_values_to_wave (sample_rate=44100, sound_np_array=np.array([]), output_filename="sound_output.wav"):
    """save adc values as Wave form file (listening file)

    Args:
        sample_rate (int): 
            determine how many samples taken per second (Hz)
        sound_np_array (array): 
            normalised Adc values b/w (-1 to 1)
        output_filename : str
            The WAVE filename to save.
    """
    with wave.open(f"{output_filename}", "wb") as waveFile:
        waveFile.setnchannels(1)
        waveFile.setsampwidth(2)
        waveFile.setframerate(sample_rate)
        waveFile.writeframes(sound_np_array.tobytes())

    print(f"Audio saved to {output_filename}")








# -------------------- Export as CSV file function --------------------

def save_adc_values_to_csv(raw_adc_values, output_filename="raw_adc_values.csv", sample_rate=44100):
    """
    Save raw ADC values to a CSV file.

    CSV columns:
        sample_index
        time_seconds
        adc_value
    """

    with open(output_filename, mode="w", newline="") as csv_file:
        writer = csv.writer(csv_file)

        writer.writerow(["sample_index", "time_seconds", "adc_value"])

        for sample_index, adc_value in enumerate(raw_adc_values):  #enumerate convert list into pairs (idx, val)
            time_seconds = sample_index / sample_rate

            writer.writerow([sample_index, time_seconds, int(adc_value)])

    print(f"Raw ADC CSV saved as {output_filename}")








# -------------------- Export as PNG file function --------------------

def save_adc_plot(raw_adc_values, output_filename="adc_plot.png", sample_rate=44100):
    """
    Save a PNG plot of raw ADC values.

    Parameters:
        raw_adc_values : list or numpy array
            The raw ADC samples to plot.
        output_filename : str
            The PNG filename to save.
        sample_rate : int or float, optional
            If provided, the x-axis will be in seconds.
            If not provided, the x-axis will be sample index.
    """

    # Convert to numpy array in case a normal Python list is passed in.
    raw_adc_values = np.array(raw_adc_values)

    # Safety check.
    if len(raw_adc_values) == 0:
        print("No ADC data to plot.")
        return

    # Create x-axis values.
    if sample_rate is not None:
        x_values = np.arange(len(raw_adc_values)) / sample_rate
        x_label = "Time (s)"
    else:
        x_values = np.arange(len(raw_adc_values))
        x_label = "Sample Index"

    # Create the plot.
    plt.figure(figsize=(12, 5))
    plt.plot(x_values, raw_adc_values)
    plt.xlabel(x_label)
    plt.ylabel("ADC Value")
    plt.title("Raw ADC Data")
    plt.grid(True)

    # Save as PNG.
    plt.savefig(output_filename, dpi=300, bbox_inches="tight") #dpi: dots per inch, bbox_inches: nice plot
    plt.close()

    print(f"Plot saved as {output_filename}")








# -------------------- Numpy array converting --------------------
def convert_sound_array(original_adc_values):
    sound_np_array = np.array(original_adc_values)

    # Normalise the ADC values into the range -1 to 1.
    # First shift the mean value in the recording down to 0.
    sound_np_array = sound_np_array - sound_np_array.mean()

    # Then divide by the new maximum so the largest value becomes 1 and lowest becomes -1.
    sound_np_array = sound_np_array / np.abs(sound_np_array).max()

    # Scale normalised audio into the 16-bit signed WAV range.
    sound_np_array = sound_np_array * 32760

    # Convert to signed 16-bit because waveFile.setsampwidth(2)
    # means each WAV sample is 2 bytes.
    sound_np_array = np.astype(sound_np_array, np.int16)
    
    return sound_np_array








# -------------------- Decode function --------------------

def decode_packed_12bit_samples(packed_data):
    """
    Decodes the ADC values received into full samples

    Args:
        list/numpy array: packed bytes from STM32

    Returns:
        list/numpy array: original adc values
    """
    original_adc_values = []

    # Keep only complete 3-byte groups.
    # Every 3 bytes contains exactly two 12-bit samples.
    usable_length = (len(packed_data) // 3) * 3         #ensure length is factor of 3 to spit bytes correctly 
    packed_data = packed_data[:usable_length]

    for i in range(0, len(packed_data), 3):
        b0 = packed_data[i]
        b1 = packed_data[i + 1]
        b2 = packed_data[i + 2]

        # Reverse of STM packing:
        #
        # STM:
        #   out[0] = sample1 bits [11:4]
        #   out[1] = sample1 bits [3:0] + sample2 bits [11:8]
        #   out[2] = sample2 bits [7:0]
        sample1 = (b0 << 4) | ((b1 >> 4) & 0x0F)            # | operator combine 8 bits of sample b0 & to 4 bits b1
        sample2 = ((b1 & 0x0F) << 8) | b2                   # combine bottom 4 bits of b1 with 8 bits of b2

        # Safety mask to keep only 12 bits.
        original_adc_values.append(sample1 & 0x0FFF)
        original_adc_values.append(sample2 & 0x0FFF)

    return original_adc_values




















# -------------------- Main function --------------------

def main():
    




    # -------------------- Serial setup --------------------

    # Iterates for all serial devices
    ports = serial.tools.list_ports.comports()

    stm_ports = None

    for port in ports:
        # Check description or hardware ID for STM identifiers
        if "STM" in port.description:
            stm_ports = port
            print(f"Found STM Device: {port.device} - {port.description}")
            
    if stm_ports is None:
        print("No STM devices found.")
        print("Plug in STM and retry...")
        exit()
    # Timeout is needed so distance mode can stop with Ctrl+C even when no bytes are being sent.
    ser = serial.Serial(stm_ports.device, 921600, timeout=0.05)





    while(True):
        
        # -------------------- User settings & Initiate Interface--------------------

        os.system('cls' if os.name == 'nt' else 'clear')
        title = 'PROXIMITY TRIGGERED DATA AQUISITION SYSTEM'
        print(title)
        title_len = len(title)
        print("="*title_len)

        # Choose mode:
        # "M" = manual fixed-time recording
        # "D" = distance-triggered recording
        while (True):
            command_letter = input("Provide desired mode\nManual (fixed time): 'M' \nDistance Triggered Recording: 'D'\n").upper()
            if command_letter != "M" and command_letter != "D":
                print("Invalid command_letter. Use 'M' or 'D'.")
                continue
            else:
                break

        sample_rate = 44100
            
        # STM sends 192 packed bytes per UART transmit (tho technically can go up to 384). 
        # Therefore Read 192 bytes at a time from the serial buffer.
        # 192 bytes = 128 samples.
        uart_read_chunk_size = 192
        



        
        # -------------------- Receive packed bytes --------------------

        packed_data = bytearray()

        if command_letter == "M":
            
            #get recording time
            while (True):
                recording_time_seconds = input("Provide Desired Recording Time (seconds):\n")
                try:
                    recording_time_seconds = float(recording_time_seconds)
                    if recording_time_seconds > 0:
                        break
                    else:
                        print("Please enter a number greater than 0")
                        continue
                except ValueError:
                    print("Please enter a valid number")
                    continue
            
            # Manual mode has a known target byte count.
            number_of_samples = int(recording_time_seconds * sample_rate)

            # Every 2 samples are packed into 3 bytes.
            total_number_of_bytes = (number_of_samples // 2) * 3

            # Start manual mode on STM & timer
            ser.write(b"M")
            start_timeM = time.perf_counter()
            
            print("\nRunning manual mode... please wait")
            
            # M mode uses the same receive style as D mode.
            # It just exits once enough packed bytes have been received.
            while len(packed_data) < total_number_of_bytes:
                current_bytes = ser.read(uart_read_chunk_size)
                packed_data.extend(current_bytes)

            # Stop STM from sending & end timer
            end_timeM = time.perf_counter()
            ser.write(b"S")
            overall_timeM = end_timeM - start_timeM
            # Manual mode may over-read because we read in 192-byte chunks.
            # Trim back to the exact number of packed bytes expected.
            if len(packed_data) > total_number_of_bytes:
                packed_data = packed_data[:total_number_of_bytes]


        elif command_letter == "D":
            # Distance mode does not know the final byte count.
            # The STM only sends audio while the object is within 10 cm.
            print("\nDistance mode started.")
            print("Press Ctrl+C to stop recording.\n")

            # Start distance-triggered mode on STM.
            ser.write(b"D")
            start_timeD = time.perf_counter()

            try:
                while True:
                    current_bytes = ser.read(uart_read_chunk_size)
                    packed_data.extend(current_bytes)

            except KeyboardInterrupt:
                # Stop STM from sending.
                end_timeD = time.perf_counter()
                ser.write(b"S")
                overall_timeD = end_timeD - start_timeD
                print("\nStopping distance mode...\n")
        
        


        
        # -------------------- Decode packed bytes --------------------

        original_adc_values = decode_packed_12bit_samples(packed_data)

        print("Received packed bytes:", len(packed_data))
        print("Decoded samples:", len(original_adc_values))

        if len(original_adc_values) == 0:
            print("\nNo audio samples received.\n")
            repeat = input("\nEnter 'Y' to continue or 'N' to exit:\n").upper()
            if(repeat == "Y"):
                #clear terminal:
                os.system('cls' if os.name == 'nt' else 'clear')
                continue
            elif(repeat == "N"): 
                print("EXITING...")
                ser.close()
                exit()
        
        #check and print received sample rate 
        if command_letter == "M":
            print(f"{(len(original_adc_values)/overall_timeM)/1000:.2f} Ksps")
        elif command_letter == "D":
            print(f"{(len(original_adc_values)/overall_timeD)/1000:.2f} Ksps")
        
        
        


        # -------------------- Print results -------------------- 
        print("\n"*2)
        title2 = 'Output File Selection'
        print(title2)
        title2_len = len(title2)
        print("="*title2_len)
        print("Output Descriptions:\nCSV: Text File format\nPNG: Graph format \n WAVE: Digital Audio\n")


        while (True):
            num_outputs = input("Provide number of outputs desired:\n")
            try:
                num_outputs = int(num_outputs)
                if num_outputs < 4 and num_outputs > 0:
                    break
                else:
                    print("Please enter a number greater than 0 and less than 4")
                    continue
            except ValueError:
                print("Please enter a valid integer")
                continue
        
        output_choices = []
        for i in range(num_outputs):
            while(True):
                output = input(f"Please specify output {i+1} format (CSV,PNG,WAVE):\n").upper()
                if output == "CSV":
                    if "CSV" not in output_choices:
                        output_choices.append("CSV")
                        file_name = input("Please input name extension for the file, 'raw_adc_valuesxxxxx.csv: '")
                        save_adc_values_to_csv(original_adc_values, output_filename=f"raw_adc_values-{file_name}.csv", sample_rate=sample_rate)
                        break
                    else:
                        print("CSV already selected, choose a different output type")
                        continue
                elif output == "PNG":
                    if "PNG" not in output_choices:
                        output_choices.append("PNG")
                        file_name = input("Please input name extension for the file, 'raw_adc_plot-xxxxx.png: '")
                        save_adc_plot(original_adc_values, output_filename=f"raw_adc_plot-{file_name}.png", sample_rate=sample_rate)
                        break
                    else:
                        print("PNG already selected, choose a different output type")
                        continue
                elif output == "WAVE":
                    if "WAVE" not in output_choices:
                        output_choices.append("WAVE")
                        file_name = input("Please input name for the file, 'xxxxx.wav: '")
                        save_normalised_adc_values_to_wave(sample_rate=sample_rate, sound_np_array=convert_sound_array(original_adc_values), output_filename=f"{file_name}.wav")
                        break
                    else:
                        print("WAVE already selected, choose a different output type")
                        continue
                else:
                    print("Please input a valid output type")
                    continue
            
        


        
        # -------------------- Cleanup / repeat -------------------- 
            
        repeat = input("\nEnter 'Y' to continue or 'N' to exit:\n").upper()

        if(repeat == "Y"):
            #clear terminal:
            continue
        else: 
            print("EXITING...")
            ser.close()
            exit()
       

    







if __name__ == "__main__":
    main()