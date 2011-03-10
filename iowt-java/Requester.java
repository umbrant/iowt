import java.io.*;
import java.net.*;
public class Requester{
	Socket requestSocket;
	DataOutputStream out;
	DataInputStream in;

	static final int SIZE = 67108864;
	//static final int SIZE = 268435456;
	byte[] buffer = new byte[SIZE];
	Requester()
	{
	}

	void run(String host)
	{
		try{
			requestSocket = new Socket(host, 8002);
			System.out.println("Connected to localhost in port 2004");
			out = new DataOutputStream(requestSocket.getOutputStream());
			in = new DataInputStream(requestSocket.getInputStream());
			out.flush();
			//3: Communicating with the server
			String request = "Hello server!";
			sendMessage(request.getBytes());

			int available = in.available();
			if(available > SIZE) {
				available = SIZE;
			}
			int bytes_read = 0;
			long start_time = System.nanoTime();
			while(bytes_read < SIZE) {
				try {
					bytes_read += in.read(buffer, bytes_read, SIZE-bytes_read);
				} catch(EOFException eof) {
					System.out.println("EOF");
					System.exit(-1);
				} catch(SocketException se) {
					System.out.println("ERROR: short read " + bytes_read + 
							" of expected " + SIZE + " bytes!");
					se.printStackTrace();
					break;
				}
			}
			long end_time = System.nanoTime();
			double request_mbs = bytes_read / Math.pow(2,20);
			double diff_secs = (double)(end_time - start_time) / (double)1000000000;

			double rate = request_mbs / diff_secs;

			//System.out.println("server " + bytes_read  + ">" + buffer);
			System.out.println("Rate: " + rate);
			Thread.sleep(10);
		}
		catch(UnknownHostException unknownHost){
			System.err.println("You are trying to connect to an unknown host!");
		}
		catch(IOException ioException){
			ioException.printStackTrace();
		}
		finally{
			//4: Closing connection
			try{
				in.close();
				out.close();
				requestSocket.close();
			}
			catch(IOException ioException){
				ioException.printStackTrace();
			}
		}
	}
	void sendMessage(byte[] msg)
	{
		try{
			out.write(msg, 0, msg.length);
			out.flush();
		}
		catch(IOException ioException){
			ioException.printStackTrace();
		}
	}
	public static void main(String args[])
	{
		Requester client = new Requester();
		client.run(args[0]);
	}
}

