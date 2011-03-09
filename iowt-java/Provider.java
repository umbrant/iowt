import java.io.*;
import java.net.*;
public class Provider{
	ServerSocket providerSocket;
	Socket connection = null;
	ByteArrayOutputStream out;
	ByteArrayInputStream in;

	byte[] buffer = new byte[100];

	Provider()
	{
		out = new ByteArrayOutputStream();
		in = new ByteArrayInputStream(buffer);
	}
	void run()
	{
		while(true) {
			try{
				providerSocket = new ServerSocket(2004, 10);
				System.out.println("Waiting for connection");
				connection = providerSocket.accept();
				System.out.println("Connection received from " + connection.getInetAddress().getHostName());
				out.flush();

				// Read in the message from the client
				int available = in.available();
				if(available > 100) {
					available = 100;
				}
				int bytes_read = in.read(buffer, 0, available);
				System.out.println("client>" + buffer);

				// Write a reply
				String msg = "bye";
				sendMessage(msg.getBytes());
			}
			catch(IOException ioException){
				ioException.printStackTrace();
			}
			finally{
				//4: Closing connection
				try{
					in.close();
					out.close();
					providerSocket.close();
				}
				catch(IOException ioException){
					ioException.printStackTrace();
				}
			}
		}
	}
	void sendMessage(byte[] msg)
	{
		try{
			out.write(msg);
			out.writeTo(connection.getOutputStream());
			out.flush();
			System.out.println("server>" + msg);
		}
		catch(IOException ioException){
			ioException.printStackTrace();
		}
	}
	public static void main(String args[])
	{
		Provider server = new Provider();
		server.run();
	}
}
