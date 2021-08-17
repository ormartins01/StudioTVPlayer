﻿using System;
using System.Collections.ObjectModel;
using System.Linq;
using System.Windows.Controls;
using StudioTVPlayer.Helpers;
using StudioTVPlayer.Model;
using StudioTVPlayer.Providers;

namespace StudioTVPlayer.ViewModel.Configuration
{
    public class ChannelsViewModel : ModifyableViewModelBase
    {
        private readonly MahApps.Metro.Controls.Dialogs.IDialogCoordinator _dialogCoordinator = MahApps.Metro.Controls.Dialogs.DialogCoordinator.Instance;

        private ChannelViewModel _selectedChannel;

        public ChannelsViewModel()
        {
            AddChannelCommand = new UiCommand(AddChannel);
            DeleteChannelCommand = new UiCommand(DeleteChannel, CanDeleteChannel);
            UnloadedCommand = new UiCommand((param) => { CommitChanges(param); });
            Channels = new ObservableCollection<ChannelViewModel>(GlobalApplicationData.Current.Configuration.Channels.Select(c => new ChannelViewModel(c)));
            foreach (var channel in Channels)
            {
                channel.Modified += (o, e) => IsModified = true;
                channel.RemoveRequested += Channel_RemoveRequested;
            }
            _selectedChannel = Channels.FirstOrDefault();
        }

        private async void Channel_RemoveRequested(object sender, EventArgs e)
        {
            var channel = sender as ChannelViewModel ?? throw new ArgumentException(nameof(sender));
            if (await _dialogCoordinator.ShowMessageAsync(MainViewModel.Instance, "Confirmation", $"Really remove channel \"{channel.Name}\"?", MahApps.Metro.Controls.Dialogs.MessageDialogStyle.AffirmativeAndNegative) != MahApps.Metro.Controls.Dialogs.MessageDialogResult.Affirmative)
                return;
            Channels.Remove(channel);
            IsModified = true;
        }

        public UiCommand AddChannelCommand { get; }
        public UiCommand DeleteChannelCommand { get; }
        public UiCommand UnloadedCommand { get; }
        public ChannelViewModel SelectedChannel
        {
            get => _selectedChannel;
            set
            {
                if (_selectedChannel == value)
                    return;
                _selectedChannel = value;
                NotifyPropertyChanged();
            }
        }

        public ObservableCollection<ChannelViewModel> Channels { get; }

        private void CommitChanges(object param)
        {
            if (param != null)
            {
                object[] parameters = param as object[];
                DataGrid dg = parameters[0] as DataGrid;
                dg.CommitEdit(DataGridEditingUnit.Row, true);
            }
        }
       
        private bool CanDeleteChannel(object obj)
        {
            if (_selectedChannel != null) return true;
            return false;
        }

        private void DeleteChannel(object obj)
        {
            var channel = obj as ChannelViewModel ?? throw new ArgumentException(nameof(obj));
            Channels.Remove(channel);            
        }        

        private void AddChannel(object obj)
        {
            var channel = new Channel { DeviceIndex = -1, Name = $"Channel {Channels.Count + 1}" };
            var vm = new ChannelViewModel(channel);
            vm.RemoveRequested += Channel_RemoveRequested;
            Channels.Add(vm);
            SelectedChannel = vm;
            IsModified = true;
        }

        public override void Apply()
        {
            foreach (var channel in Channels)
                channel.Apply();
            GlobalApplicationData.Current.UpdateChannels(Channels.Select(c => c.Channel).ToList());
        }

        public override bool IsValid()
        {
            return Channels.All(c => c.IsValid());
        }
    }
}
