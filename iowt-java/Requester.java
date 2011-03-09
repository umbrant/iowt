import java.io.*;
import java.net.*;
public class Requester{
	Socket requestSocket;
	ByteArrayOutputStream out;
	ByteArrayInputStream in;

	byte[] buffer = new byte[100];
	Requester()
	{
		out = new ByteArrayOutputStream();
		in = new ByteArrayInputStream(buffer);
	}

	void run()
	{
		try{
			requestSocket = new Socket("localhost", 2004);
			System.out.println("Connected to localhost in port 2004");
			//out = new ByteArrayOutputStream()requestSocket.getOutputStream());
			//in = new ByteArrayInputStream(requestSocket.getInputStream());
			out.flush();
			//3: Communicating with the server
			String request = "Hello server!";
			sendMessage(request.getBytes());

			int available = in.available();
			if(available > 100) {
				available = 100;
			}
			int bytes_read = in.read(buffer, 0, available);
			System.out.println("server>" + buffer);
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
			out.write(msg);
			out.writeTo(requestSocket.getOutputStream());
			out.flush();
			System.out.println("server>" + msg);
		}
		catch(IOException ioException){
			ioException.printStackTrace();
		}
	}
	public static void main(String args[])
	{
		Requester client = new Requester();
		client.run();
	}
}

