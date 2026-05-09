import numpy as np
import matplotlib.pyplot as plt
import csv
import wave
import serial
import serial.tools.list_ports
import time

# -------------------- Export as Wave file function --------------------
def save_normalised_adc_values_to_wave (sample_rate, sound_np_array):
    """save adc values as Wave form file (listening file)

    Args:
        sample_rate (int): determine how many samples taken per second (Hz)
        sound_np_array (array): noralised Adc values b/w (-1 to 1)
    """
    with wave.open("test1.wav", "wb") as waveFile:
        waveFile.setnchannels(1)
        waveFile.setsampwidth(2)
        waveFile.setframerate(sample_rate)
        waveFile.writeframes(sound_np_array.tobytes())

    print("Audio saved to test1.wav")


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

            writer.writerow([
                sample_index,
                time_seconds,
                int(adc_value)
            ])

    print(f"Raw ADC CSV saved as {output_filename}")





# -------------------- Export as PNG file function --------------------

def save_adc_plot(raw_adc_values, output_filename="adc_plot.png", sample_rate=None):
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
    """_summary_

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
    # Timeout is needed so distance mode can stop with Ctrl+C even when no bytes are being sent.
    ser = serial.Serial(stm_ports.device, 921600, timeout=0.05)


    while(True):
        
        # -------------------- User settings & Initiate Interface--------------------

        # Choose mode:
        # "M" = manual fixed-time recording
        # "D" = distance-triggered recording
        print("\n"*3)
        title = 'PROXIMITY TRIGGERED DATA AQUISITION SYSTEM'
        print(title)
        title_len = len(title)
        print("="*title_len)
        command_letter = input("Provide desired mode\nManual (fixed time): 'M' \nDistance Triggered Recording: 'D'\n")
        
        sample_rate = 44100
            
        # STM sends 192 packed bytes per UART transmit. 
        # 192 bytes = 128 samples.
        uart_read_chunk_size = 192
        
        
        # -------------------- Receive packed bytes --------------------

        packed_data = bytearray()

        if command_letter == "M":
            
            #get recording time
            recording_time_seconds = int(input("Provide Desired Recording Time (seconds):\n"))
            
            # Manual mode has a known target byte count.
            number_of_samples = recording_time_seconds * sample_rate

            # Every 2 samples are packed into 3 bytes.
            total_number_of_bytes = (number_of_samples // 2) * 3

            # Start manual mode on STM.
            start_time = time.perf_counter()
            ser.write(b"M")
            
            print("\nRunning manual mode... please wait")
            
            # M mode uses the same receive style as D mode.
            # It just exits once enough packed bytes have been received.
            while len(packed_data) < total_number_of_bytes:
                current_bytes = ser.read(uart_read_chunk_size)
                packed_data.extend(current_bytes)

            # Stop STM from sending.
            ser.write(b"S")
            end_time = time.perf_counter()
            overall_time = end_time - start_time
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

            try:
                while True:
                    current_bytes = ser.read(uart_read_chunk_size)
                    packed_data.extend(current_bytes)

            except KeyboardInterrupt:
                # Stop STM from sending.
                ser.write(b"S")
                print("\nStopping distance mode...\n")



        else:
            print("Invalid command_letter. Use 'M' or 'D'.")
            ser.close()
            exit()
        
        
        
        # -------------------- Decode packed bytes --------------------

        original_adc_values = decode_packed_12bit_samples(packed_data)

        print("Received packed bytes:", len(packed_data))
        print("Decoded samples:", len(original_adc_values))

        if len(original_adc_values) == 0:
            print("\nNo audio samples received.\n")
            ser.close()
            exit()
 
        print(f"{(len(original_adc_values)/overall_time)/1000:2f} Ksps")
        # -------------------- Print results -------------------- 
        print("\n Output Descriptions:\n CSV: Text File format\n PNG: Graph format \n WAVE: Digital Audio\n")
        num_outputs = int(input("Provide number of outputs desired:\n"))

        
        for i in range(num_outputs):
            
            while(True):
                output = input(f"Please specify output {i+1} format (CSV,PNG,WAVE):\n").upper()
                if output == "CSV":
                    
                    save_adc_values_to_csv(original_adc_values, output_filename="raw_adc_values4.csv", sample_rate=sample_rate)
                    break
                elif output == "PNG":
                    
                    save_adc_plot(original_adc_values, output_filename="adc_plot4.png", sample_rate=sample_rate)
                    break
                elif output == "WAVE":
                    
                    save_normalised_adc_values_to_wave (sample_rate = sample_rate, sound_np_array = convert_sound_array(original_adc_values))
                    break
                else:
                    print("Invalid input...Restart")
                    continue
            
        
        
        # -------------------- Cleanup / repeat -------------------- 
            
        repeat = input("\nEnter 'Y' to continue or 'N' to exit:\n")
    
        if(repeat == "Y"):
            continue
        
        elif(repeat == "N"): 
            print("EXITING...")
            ser.close()
            exit()
       

    
if __name__ == "__main__":
    main()